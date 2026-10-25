// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The reading of the classic table, the cross-reference stream and the /Prev
// chain. Every failure is phrased as the reason a repair report gives, since
// it is what the document records when it reconstructs.

#include "syntax/cross_reference.hpp"

#include "syntax/filters.hpp"
#include "syntax/lexer.hpp"
#include "syntax/parser.hpp"
#include "syntax/stream.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <utility>

namespace pdfkit::syntax {

namespace {

constexpr std::size_t startxref_window = 2048;

std::string at_byte(std::uint64_t offset) {
  return " at byte " + std::to_string(offset);
}

}  // namespace

CrossReference::CrossReference(std::map<std::uint32_t, Entry> entries, Dictionary trailer)
    : entries_(std::move(entries)), trailer_(std::move(trailer)) {}

const CrossReference::Entry* CrossReference::find(std::uint32_t number) const noexcept {
  const auto found = entries_.find(number);
  return found == entries_.end() ? nullptr : &found->second;
}

void CrossReference::merge_trailer(const Dictionary& older) {
  for (const auto& [key, value] : older.entries()) {
    if (key != "Prev" && key != "XRefStm" && !trailer_.contains(key)) {
      trailer_.set(key, value);
    }
  }
}

std::optional<CrossReference> CrossReference::read(platform::ByteSpan file,
                                                   platform::Budget& budget,
                                                   std::string& failure_out) {
  // startxref within the last 2,048 bytes (docs/DESIGN.md section 1.4).
  const std::size_t window_start =
      file.size() > startxref_window ? file.size() - startxref_window : 0;
  std::optional<std::size_t> last;
  for (std::optional<std::size_t> found = Parser::find_keyword(file, window_start, "startxref");
       found; found = Parser::find_keyword(file, *found + 1, "startxref")) {
    last = found;
  }
  if (!last) {
    failure_out = "startxref is missing from the last 2,048 bytes";
    return std::nullopt;
  }
  Lexer lexer(file, *last + 9);
  const Lexer::Token value = lexer.next();
  if (value.kind != Lexer::TokenKind::integer || value.integer < 0) {
    failure_out = "startxref is not followed by an offset";
    return std::nullopt;
  }

  CrossReference table;
  std::set<std::uint64_t> visited;
  auto offset = static_cast<std::uint64_t>(value.integer);
  bool first = true;
  while (true) {
    if (offset >= file.size()) {
      failure_out = first ? "startxref " + std::to_string(offset) + " points past the end"
                          : "a /Prev offset " + std::to_string(offset) + " points past the end";
      return std::nullopt;
    }
    if (!visited.insert(offset).second) {
      // A loop in the chain ends it; every section was read once.
      table.notes_.push_back("the /Prev chain loops back to byte " + std::to_string(offset));
      break;
    }
    Lexer probe(file, static_cast<std::size_t>(offset));
    const Lexer::Token token = probe.next();
    Dictionary trailer;
    std::string failure;
    if (token.is_keyword("xref") && token.start == offset) {
      failure = table.read_table(file, offset, first, trailer);
    } else if (token.kind == Lexer::TokenKind::integer && token.start == offset &&
               Parser(file).read_header(static_cast<std::size_t>(offset))) {
      failure = table.read_stream(file, offset, budget, trailer);
    } else {
      failure = (first ? std::string("startxref ") : std::string("a /Prev offset ")) +
                std::to_string(offset) + " does not land on xref or a cross-reference stream";
    }
    if (!failure.empty()) {
      failure_out = std::move(failure);
      return std::nullopt;
    }
    table.sections_.push_back(offset);
    if (first) {
      table.trailer_ = trailer;
    } else {
      table.merge_trailer(trailer);
    }
    // A hybrid file's stream fills only what its table left unnamed, before
    // the older sections (ISO 32000-1 section 7.5.8.4).
    if (const auto stream_offset = trailer.get("XRefStm").integer()) {
      Dictionary ignored;
      if (*stream_offset < 0 || static_cast<std::uint64_t>(*stream_offset) >= file.size() ||
          !table.read_stream(file, static_cast<std::uint64_t>(*stream_offset), budget, ignored)
               .empty()) {
        table.notes_.push_back("the /XRefStm offset " + std::to_string(*stream_offset) +
                               " names no cross-reference stream and is ignored");
      }
    }
    const auto previous = trailer.get("Prev").integer();
    if (!previous) {
      break;
    }
    if (*previous < 0) {
      failure_out = "a /Prev offset is negative";
      return std::nullopt;
    }
    offset = static_cast<std::uint64_t>(*previous);
    first = false;
  }
  return table;
}

std::string CrossReference::read_table(platform::ByteSpan file, std::uint64_t offset,
                                       bool first_section, Dictionary& trailer) {
  Lexer lexer(file, static_cast<std::size_t>(offset));
  (void)lexer.next();  // xref
  bool first_subsection = true;
  while (true) {
    const Lexer::Token token = lexer.next();
    if (token.is_keyword("trailer")) {
      std::vector<std::string> faults;
      const Lexer::Token open = lexer.next();
      const Object dictionary = Parser::parse_value(lexer, open, true, faults);
      if (dictionary.dictionary() == nullptr) {
        return "the trailer" + at_byte(token.start) + " is not a dictionary";
      }
      trailer = *dictionary.dictionary();
      return {};
    }
    if (token.kind != Lexer::TokenKind::integer || token.integer < 0) {
      return token.kind == Lexer::TokenKind::end
                 ? "the cross-reference table" + at_byte(offset) + " has no trailer"
                 : "the cross-reference table is malformed" + at_byte(token.start);
    }
    const Lexer::Token count = lexer.next();
    if (count.kind != Lexer::TokenKind::integer || count.integer < 0) {
      return "a cross-reference subsection header is malformed" + at_byte(token.start);
    }
    auto number = static_cast<std::uint64_t>(token.integer);
    for (std::int64_t i = 0; i < count.integer; ++i) {
      const std::size_t mark = lexer.position();
      const Lexer::Token field_offset = lexer.next();
      const Lexer::Token field_generation = lexer.next();
      const Lexer::Token kind = lexer.next();
      const bool in_use = kind.is_keyword("n");
      if (field_offset.kind != Lexer::TokenKind::integer ||
          field_generation.kind != Lexer::TokenKind::integer ||
          (!in_use && !kind.is_keyword("f")) || field_offset.integer < 0 ||
          field_generation.integer < 0) {
        // A subsection shorter than its count: the next token starts the next
        // subsection or the trailer.
        lexer.seek(mark);
        notes_.push_back("a cross-reference subsection" + at_byte(token.start) + " holds " +
                         std::to_string(i) + " of the " + std::to_string(count.integer) +
                         " entries it claims");
        break;
      }
      if (i == 0 && first_subsection && first_section && number == 1 && !in_use &&
          field_generation.integer == 65535) {
        // pdf.js's correction (recalled): the free head of the list labelled 1.
        number = 0;
        notes_.emplace_back(
            "the first cross-reference subsection is labelled 1 but starts with "
            "the free entry of object 0, and is read from 0");
      }
      const std::uint64_t this_number = number + static_cast<std::uint64_t>(i);
      if (this_number > 0xFFFFFFFFULL) {
        return "a cross-reference subsection names an object number past 2^32";
      }
      Entry entry;
      if (in_use && field_offset.integer > 0) {
        entry.type = Entry::Type::offset;
        entry.offset = static_cast<std::uint64_t>(field_offset.integer);
        entry.generation =
            static_cast<std::uint32_t>(std::min<std::int64_t>(field_generation.integer, 65535));
      }
      entries_.try_emplace(static_cast<std::uint32_t>(this_number), entry);
    }
    first_subsection = false;
  }
}

std::string CrossReference::read_stream(platform::ByteSpan file, std::uint64_t offset,
                                        platform::Budget& budget, Dictionary& trailer) {
  const auto indirect = Parser(file).parse_indirect(static_cast<std::size_t>(offset), {});
  const Stream* stream = indirect ? indirect->object.stream() : nullptr;
  if (stream == nullptr) {
    return "the object" + at_byte(offset) + " is not a cross-reference stream";
  }
  const Dictionary& dictionary = stream->dictionary();
  const Object::Array* widths = dictionary.get("W").array();
  if (widths == nullptr || widths->size() < 3) {
    return "the cross-reference stream" + at_byte(offset) + " has no /W of three widths";
  }
  std::array<std::size_t, 3> width{};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto value = (*widths)[i].integer();
    if (!value || *value < 0 || *value > 8) {
      return "a /W width of the cross-reference stream" + at_byte(offset) +
             " is not an integer from 0 to 8";
    }
    width.at(i) = static_cast<std::size_t>(*value);
  }
  const std::size_t row = width[0] + width[1] + width[2];
  const Filters::Result decoded = Filters::decode(
      file.subspan(stream->data_offset(), stream->data_length()), dictionary.get("Filter"),
      dictionary.get("DecodeParms"), budget, "the cross-reference stream" + at_byte(offset));
  if (!decoded.remaining.empty()) {
    return "the cross-reference stream" + at_byte(offset) + " uses the filter " +
           decoded.remaining.front();
  }
  if (row == 0) {
    return "the cross-reference stream" + at_byte(offset) + " has rows of zero bytes";
  }
  std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
  if (const Object::Array* index = dictionary.get("Index").array()) {
    for (std::size_t i = 0; i + 1 < index->size(); i += 2) {
      const auto start = (*index)[i].integer();
      const auto count = (*index)[i + 1].integer();
      if (!start || !count || *start < 0 || *count < 0) {
        return "the /Index of the cross-reference stream" + at_byte(offset) + " is malformed";
      }
      ranges.emplace_back(*start, *count);
    }
  } else {
    const auto size = dictionary.get("Size").integer();
    if (!size || *size < 0) {
      return "the cross-reference stream" + at_byte(offset) + " has no /Size";
    }
    ranges.emplace_back(0, *size);
  }
  const auto field = [&](std::size_t at, std::size_t length) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < length; ++i) {
      value = (value << 8U) | std::to_integer<std::uint64_t>(decoded.bytes[at + i]);
    }
    return value;
  };
  std::size_t at = 0;
  for (const auto& [start, count] : ranges) {
    for (std::int64_t i = 0; i < count; ++i) {
      if (decoded.bytes.size() - at < row) {
        notes_.push_back("the cross-reference stream" + at_byte(offset) +
                         " holds fewer rows than its /Index names");
        trailer = dictionary;
        return {};
      }
      const std::uint64_t type = width[0] == 0 ? 1 : field(at, width[0]);
      const std::uint64_t second = field(at + width[0], width[1]);
      const std::uint64_t third = field(at + width[0] + width[1], width[2]);
      at += row;
      const std::int64_t number = start + i;
      if (number > 0xFFFFFFFFLL) {
        return "the cross-reference stream" + at_byte(offset) + " names a number past 2^32";
      }
      Entry entry;
      if (type == 1 && second > 0) {
        entry.type = Entry::Type::offset;
        entry.offset = second;
        entry.generation = static_cast<std::uint32_t>(std::min<std::uint64_t>(third, 65535));
      } else if (type == 2) {
        entry.type = Entry::Type::compressed;
        entry.offset = second;
        entry.generation =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(third, 0xFFFFFFFFULL));
      } else if (type > 2) {
        // ISO 32000-1 section 7.5.8.3: an unknown type is a reference to null.
        continue;
      }
      entries_.try_emplace(static_cast<std::uint32_t>(number), entry);
    }
  }
  trailer = dictionary;
  return {};
}

}  // namespace pdfkit::syntax
