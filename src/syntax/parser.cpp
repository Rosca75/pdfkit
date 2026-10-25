// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The object grammar of ISO 32000-1 sections 7.3 and 7.3.10 (read) over the
// lexer's tokens, with the recoveries of docs/DESIGN.md section 1.3.

#include "syntax/parser.hpp"

#include "syntax/dictionary.hpp"
#include "syntax/stream.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace pdfkit::syntax {

namespace {

constexpr unsigned cr = 0x0D;
constexpr unsigned lf = 0x0A;

[[nodiscard]] unsigned at(platform::ByteSpan bytes, std::size_t index) noexcept {
  return index < bytes.size() ? std::to_integer<unsigned>(bytes[index]) : 0x100U;
}

// Keywords that end an object or start the next file structure; met inside a
// container, they end it rather than being skipped.
[[nodiscard]] bool is_structural(const Lexer::Token& token) noexcept {
  return token.kind == Lexer::TokenKind::keyword &&
         (token.text == "endobj" || token.text == "stream" || token.text == "endstream" ||
          token.text == "obj" || token.text == "xref" || token.text == "trailer" ||
          token.text == "startxref");
}

// The first occurrence of pattern at or after from, anywhere in the bytes.
[[nodiscard]] std::optional<std::size_t> find_raw(platform::ByteSpan bytes, std::size_t from,
                                                  std::string_view pattern) noexcept {
  if (pattern.empty() || from >= bytes.size()) {
    return std::nullopt;
  }
  const auto* first = reinterpret_cast<const char*>(bytes.data());
  const std::string_view text(first, bytes.size());
  const std::size_t found = text.find(pattern, from);
  if (found == std::string_view::npos) {
    return std::nullopt;
  }
  return found;
}

std::string describe(Object::Reference reference) {
  return "object " + std::to_string(reference.number) + " " + std::to_string(reference.generation);
}

}  // namespace

std::optional<std::size_t> Parser::find_keyword(platform::ByteSpan bytes, std::size_t from,
                                                std::string_view word) {
  std::size_t position = from;
  while (true) {
    const std::optional<std::size_t> found = find_raw(bytes, position, word);
    if (!found) {
      return std::nullopt;
    }
    const std::size_t end = *found + word.size();
    const bool before = *found == 0 || !Lexer::is_regular(bytes[*found - 1]);
    const bool after = end >= bytes.size() || !Lexer::is_regular(bytes[end]);
    if (before && after) {
      return found;
    }
    position = *found + 1;
  }
}

Object Parser::parse_value(Lexer& lexer, const Lexer::Token& first, bool references,
                           std::vector<std::string>& repairs, int depth) {
  using Kind = Lexer::TokenKind;
  if (first.repaired) {
    repairs.emplace_back("a malformed token at byte " + std::to_string(first.start) +
                         " was read leniently");
  }
  switch (first.kind) {
    case Kind::integer: {
      if (references && std::in_range<std::uint32_t>(first.integer)) {
        const std::size_t mark = lexer.position();
        const Lexer::Token generation = lexer.next();
        if (generation.kind == Kind::integer && std::in_range<std::uint32_t>(generation.integer)) {
          const Lexer::Token keyword = lexer.next();
          if (keyword.is_keyword("R")) {
            return Object::make_reference({static_cast<std::uint32_t>(first.integer),
                                           static_cast<std::uint32_t>(generation.integer)});
          }
        }
        lexer.seek(mark);
      }
      return Object::make_integer(first.integer);
    }
    case Kind::real:
      return Object::make_real(first.real);
    case Kind::literal_string:
      return Object::make_string(first.text, false);
    case Kind::hex_string:
      return Object::make_string(first.text, true);
    case Kind::name:
      return Object::make_name(first.text);
    case Kind::keyword:
      if (first.text == "true") {
        return Object::make_boolean(true);
      }
      if (first.text == "false") {
        return Object::make_boolean(false);
      }
      if (first.text != "null") {
        repairs.emplace_back("the keyword '" + first.text + "' at byte " +
                             std::to_string(first.start) + " is not a value and reads as null");
      }
      return {};
    case Kind::array_open: {
      if (depth >= max_depth) {
        repairs.emplace_back("containers nest deeper than " + std::to_string(max_depth) +
                             " at byte " + std::to_string(first.start));
        return Object::make_array({});
      }
      Object::Array items;
      while (true) {
        const std::size_t mark = lexer.position();
        const Lexer::Token token = lexer.next();
        if (token.kind == Kind::array_close) {
          break;
        }
        if (token.kind == Kind::end || is_structural(token) || token.kind == Kind::dict_close) {
          repairs.emplace_back("an array opened at byte " + std::to_string(first.start) +
                               " is not closed");
          if (token.kind != Kind::end) {
            lexer.seek(mark);
          }
          break;
        }
        if (token.kind == Kind::stray || token.kind == Kind::brace_open ||
            token.kind == Kind::brace_close) {
          repairs.emplace_back("a stray delimiter at byte " + std::to_string(token.start) +
                               " is skipped");
          continue;
        }
        items.push_back(parse_value(lexer, token, references, repairs, depth + 1));
      }
      return Object::make_array(std::move(items));
    }
    case Kind::dict_open: {
      if (depth >= max_depth) {
        repairs.emplace_back("containers nest deeper than " + std::to_string(max_depth) +
                             " at byte " + std::to_string(first.start));
        return Object::make_dictionary({});
      }
      Dictionary dictionary;
      while (true) {
        const std::size_t mark = lexer.position();
        const Lexer::Token key = lexer.next();
        if (key.kind == Kind::dict_close) {
          break;
        }
        if (key.kind == Kind::end || is_structural(key)) {
          repairs.emplace_back("a dictionary opened at byte " + std::to_string(first.start) +
                               " is not closed");
          if (key.kind != Kind::end) {
            lexer.seek(mark);
          }
          break;
        }
        if (key.kind != Kind::name) {
          repairs.emplace_back("a dictionary key at byte " + std::to_string(key.start) +
                               " is not a name and is skipped");
          if (key.kind == Kind::array_open || key.kind == Kind::dict_open) {
            (void)parse_value(lexer, key, references, repairs, depth + 1);
          }
          continue;
        }
        const std::size_t value_mark = lexer.position();
        const Lexer::Token value = lexer.next();
        if (value.kind == Kind::dict_close || value.kind == Kind::end || is_structural(value)) {
          repairs.emplace_back("the key /" + key.text + " at byte " + std::to_string(key.start) +
                               " has no value");
          lexer.seek(value_mark);
          continue;
        }
        dictionary.set(key.text, parse_value(lexer, value, references, repairs, depth + 1));
      }
      return Object::make_dictionary(std::move(dictionary));
    }
    case Kind::end:
      repairs.emplace_back("the data ends where a value was expected");
      return {};
    case Kind::array_close:
    case Kind::dict_close:
    case Kind::brace_open:
    case Kind::brace_close:
    case Kind::stray:
      repairs.emplace_back("a stray delimiter at byte " + std::to_string(first.start) +
                           " reads as null");
      return {};
  }
  return {};
}

Object Parser::parse_direct(std::size_t offset, std::vector<std::string>& repairs) const {
  Lexer lexer(bytes_, offset);
  const Lexer::Token first = lexer.next();
  return parse_value(lexer, first, true, repairs);
}

std::optional<std::pair<Object::Reference, std::size_t>> Parser::read_header(
    std::size_t offset) const {
  Lexer lexer(bytes_, offset);
  const Lexer::Token number = lexer.next();
  if (number.kind != Lexer::TokenKind::integer || !std::in_range<std::uint32_t>(number.integer)) {
    return std::nullopt;
  }
  const Lexer::Token generation = lexer.next();
  if (generation.kind != Lexer::TokenKind::integer ||
      !std::in_range<std::uint32_t>(generation.integer)) {
    return std::nullopt;
  }
  const Lexer::Token keyword = lexer.next();
  if (!keyword.is_keyword("obj")) {
    return std::nullopt;
  }
  return std::pair{Object::Reference{static_cast<std::uint32_t>(number.integer),
                                     static_cast<std::uint32_t>(generation.integer)},
                   keyword.end};
}

std::optional<Parser::Indirect> Parser::parse_indirect(std::size_t offset,
                                                       const LengthResolver& resolve_length) const {
  const auto header = read_header(offset);
  if (!header) {
    return std::nullopt;
  }
  Indirect result;
  result.reference = header->first;
  result.start = Lexer(bytes_, offset).next().start;
  const std::string name = describe(result.reference);
  Lexer lexer(bytes_, header->second);
  const Lexer::Token first = lexer.next();
  if (is_structural(first)) {
    result.repairs.push_back(name + " has no value and reads as null");
    lexer.seek(first.start);
  } else {
    std::vector<std::string> faults;
    result.object = parse_value(lexer, first, true, faults);
    for (const std::string& fault : faults) {
      std::string reason = name;
      reason += ": ";
      reason += fault;
      result.repairs.push_back(std::move(reason));
    }
  }

  const std::size_t after_value = lexer.position();
  Lexer::Token next = lexer.next();
  // A keyword that only starts with "stream" is the keyword followed by a
  // damaged end of line (a stray byte before the LF), read as the keyword
  // with a repair.
  if (next.kind == Lexer::TokenKind::keyword && next.text.starts_with("stream") &&
      result.object.dictionary() != nullptr) {
    if (next.text != "stream") {
      result.repairs.push_back(name + ": its stream keyword is followed by stray bytes");
    }
    // The data starts after one CR LF, one LF or a lone CR (ISO 32000-1 section
    // 7.3.8.1; the lone CR accepted as viewers accept it). Spaces before the end
    // of line are tolerated.
    std::size_t data_start = next.end;
    std::size_t probe = data_start;
    while (at(bytes_, probe) == ' ' || at(bytes_, probe) == '\t') {
      ++probe;
    }
    if (at(bytes_, probe) == cr && at(bytes_, probe + 1) == lf) {
      data_start = probe + 2;
    } else if (at(bytes_, probe) == lf || at(bytes_, probe) == cr) {
      data_start = probe + 1;
    }
    const Dictionary& dictionary = *result.object.dictionary();
    std::optional<std::int64_t> length;
    const Object& length_object = dictionary.get("Length");
    if (const auto value = length_object.integer()) {
      length = value;
    } else if (const auto reference = length_object.reference(); reference && resolve_length) {
      length = resolve_length(*reference);
    }
    std::optional<std::size_t> data_end;
    std::size_t after_data = 0;
    if (length && *length >= 0 &&
        static_cast<std::uint64_t>(*length) <=
            bytes_.size() - std::min(data_start, bytes_.size())) {
      std::size_t check = data_start + static_cast<std::size_t>(*length);
      while (check < bytes_.size() && Lexer::is_white_space(bytes_[check])) {
        ++check;
      }
      if (find_raw(bytes_, check, "endstream") == check) {
        data_end = data_start + static_cast<std::size_t>(*length);
        after_data = check + 9;
      }
    }
    if (!data_end) {
      // pdf.js's and QPDF's recovery: the data ends at the next endstream (or,
      // when it is missing, at endobj), less the end of line before it.
      const auto endstream = find_raw(bytes_, data_start, "endstream");
      const auto endobj = find_raw(bytes_, data_start, "endobj");
      std::size_t stop = bytes_.size();
      if (endstream && (!endobj || *endstream < *endobj)) {
        stop = *endstream;
        after_data = *endstream + 9;
        result.repairs.push_back(name + ": its /Length " +
                                 (length ? std::to_string(*length) + " does not end at endstream"
                                         : std::string("is missing or unusable")) +
                                 "; the data ends at the endstream found by scanning");
      } else if (endobj) {
        stop = *endobj;
        after_data = *endobj;
        result.repairs.push_back(name + ": endstream is missing; the data ends before endobj");
      } else {
        after_data = bytes_.size();
        result.repairs.push_back(
            name + ": neither endstream nor endobj follows; the data runs to the end");
      }
      if (stop > data_start && at(bytes_, stop - 1) == lf) {
        --stop;
      }
      if (stop > data_start && at(bytes_, stop - 1) == cr) {
        --stop;
      }
      data_end = std::max(stop, data_start);
    }
    const std::size_t start = std::min(data_start, bytes_.size());
    result.object = Object::make_stream(
        Stream(dictionary, start, std::max(*data_end, start) - start, result.reference));
    lexer.seek(after_data);
    next = lexer.next();
  }

  if (next.is_keyword("endobj")) {
    result.end = next.end;
  } else {
    result.repairs.push_back(name + ": endobj is missing");
    result.end = next.kind == Lexer::TokenKind::end ? bytes_.size() : next.start;
    if (next.kind != Lexer::TokenKind::end && result.end < after_value) {
      result.end = after_value;
    }
  }
  return result;
}

}  // namespace pdfkit::syntax
