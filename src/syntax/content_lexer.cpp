// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The content-stream tokeniser: operands are parsed with the object parser
// (references off), operators end an operation, and the inline image's data
// is delimited by the rules of docs/DESIGN.md chapter 4. A recovery that can
// take text is kept as a loss beside the count of every recovery.

#include "syntax/content_lexer.hpp"

#include "platform/checked.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/filters.hpp"
#include "syntax/parser.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace pdfkit::syntax {

namespace {

// Operands past this count without an operator are dropped as a fault.
constexpr std::size_t max_operands = 65536;
// Inline images wider or taller than this have no computable length.
constexpr std::int64_t max_inline_side = 1048576;

// The operators of ISO 32000-1 Annex A, table A.1 (read).
constexpr std::array<std::string_view, 73> operators{
    "b",  "B",  "b*", "B*", "BDC", "BI",  "BMC", "BT", "BX", "c",  "cm", "CS", "cs", "d",  "d0",
    "d1", "Do", "DP", "EI", "EMC", "ET",  "EX",  "f",  "F",  "f*", "G",  "g",  "gs", "h",  "i",
    "ID", "j",  "J",  "K",  "k",   "l",   "m",   "M",  "MP", "n",  "q",  "Q",  "re", "RG", "rg",
    "ri", "s",  "S",  "SC", "sc",  "SCN", "scn", "sh", "T*", "Tc", "Td", "TD", "Tf", "Tj", "TJ",
    "TL", "Tm", "Tr", "Ts", "Tw",  "Tz",  "v",   "w",  "W",  "W*", "y",  "'",  "\""};

[[nodiscard]] const Object& entry_of(const Dictionary& dictionary, std::string_view abbreviated,
                                     std::string_view full) {
  const Object* value = dictionary.find(abbreviated);
  return value != nullptr ? *value : dictionary.get(full);
}

[[nodiscard]] std::vector<std::string> filter_names(const Dictionary& dictionary) {
  std::vector<std::string> names;
  const Object& filter = entry_of(dictionary, "F", "Filter");
  if (const std::string* name = filter.name()) {
    names.emplace_back(Filters::full_name(*name));
  } else if (const Object::Array* list = filter.array()) {
    for (const Object& item : *list) {
      const std::string* item_name = item.name();
      names.emplace_back(item_name != nullptr ? std::string(Filters::full_name(*item_name))
                                              : std::string());
    }
  }
  return names;
}

[[nodiscard]] std::optional<std::size_t> find_text(platform::ByteSpan bytes, std::size_t from,
                                                   std::string_view pattern) noexcept {
  if (from >= bytes.size()) {
    return std::nullopt;
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  const std::size_t found = text.find(pattern, from);
  return found == std::string_view::npos ? std::nullopt : std::optional<std::size_t>(found);
}

}  // namespace

ContentLexer::ContentLexer(const std::vector<std::vector<std::byte>>& pieces, const Limits& limits,
                           std::string what)
    : lexer_(platform::ByteSpan()), limits_(&limits), what_(std::move(what)) {
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    if (i > 0) {
      joined_.push_back(std::byte{'\n'});
    }
    piece_starts_.push_back(joined_.size());
    joined_.insert(joined_.end(), pieces[i].begin(), pieces[i].end());
  }
  lexer_ = Lexer(joined_);
}

bool ContentLexer::is_operator(std::string_view keyword) noexcept {
  return std::ranges::find(operators, keyword) != operators.end();
}

ContentLexer::Location ContentLexer::locate(std::size_t offset) const noexcept {
  if (piece_starts_.empty()) {
    return {};
  }
  const auto after = std::ranges::upper_bound(piece_starts_, offset);
  const auto piece = static_cast<std::size_t>(after - piece_starts_.begin()) - 1;
  return {piece, offset - piece_starts_[piece]};
}

bool ContentLexer::lose(std::string_view damage, std::size_t start,
                        std::optional<std::size_t> bytes) {
  const std::size_t piece = locate(start).piece;
  // Losses arrive in byte order, so the entry of this piece and class, when
  // there is one, lies after every entry of an earlier piece.
  for (auto loss = losses_.rbegin(); loss != losses_.rend() && loss->piece == piece; ++loss) {
    if (loss->damage == damage) {
      loss->count = platform::saturating_add(loss->count, std::size_t{1});
      if (loss->bytes && bytes) {
        loss->bytes = platform::saturating_add(*loss->bytes, *bytes);
      } else {
        loss->bytes.reset();
      }
      return false;
    }
  }
  losses_.push_back({std::string(damage), std::string(), start, bytes, 1, piece});
  return true;
}

void ContentLexer::lose_recoveries(const std::vector<Parser::Recovery>& recoveries,
                                   std::size_t start) {
  // The parser's recoveries that drop a token which could be drawing: a
  // keyword read as null (an operator inside an array or a dictionary), a
  // key that is not a name, a container past the nesting limit. The others
  // (a reference written in content, a stray delimiter, a container left
  // open, a key without a value, a token read leniently) drop nothing an
  // operator draws.
  using Why = Parser::Recovery::Kind;
  for (const Parser::Recovery& recovery : recoveries) {
    if ((recovery.kind == Why::keyword_value || recovery.kind == Why::nesting ||
         recovery.kind == Why::key_not_name) &&
        lose("unlexable_content", start, std::nullopt)) {
      losses_.back().fault = what_ + ": " + recovery.message;
    }
  }
  if (lexer_.string_ran_to_end() && !string_lost_) {
    string_lost_ = true;
    if (lose("unlexable_content", start, joined_.size() - std::min(start, joined_.size()))) {
      losses_.back().fault = what_ + ": a string opened at byte " + std::to_string(start) +
                             " runs to the end of the content, which is read as its text";
    }
  }
}

bool ContentLexer::draws_nothing(const Operation& operation) const {
  // No operand, or numbers alone ("n g R" written bare among them): a
  // mangled keyword or an operator misspelt (CT for BT) whose operands could
  // carry no text and name no resource.
  if (std::ranges::all_of(operation.operands,
                          [](const Object& operand) { return operand.number().has_value(); })) {
    return true;
  }
  // "/Name n g R" before a marked-content operator (/OC 5 0 R BDC): the
  // reference takes the operands of BDC or DP, which draws nothing. Before
  // any other operator (a Tj, a Do, a Tf) it takes operands that drew or
  // decided text.
  const std::vector<Object>& operands = operation.operands;
  if (operation.op != "R" || operands.size() != 3 || operands[0].name() == nullptr ||
      !operands[1].integer().has_value() || !operands[2].integer().has_value()) {
    return false;
  }
  const std::string next = next_operator(operation.end);
  return next == "BDC" || next == "DP";
}

std::string ContentLexer::next_operator(std::size_t position) const {
  // A bounded look ahead, so a long run of operands costs no more than it
  // costs the next call; past it the operator is taken as unknown.
  constexpr int max_tokens = 64;
  Lexer probe(joined_, position);
  for (int i = 0; i < max_tokens; ++i) {
    const Lexer::Token token = probe.next();
    if (token.kind == Lexer::TokenKind::end) {
      break;
    }
    if (token.kind == Lexer::TokenKind::keyword && token.text != "true" && token.text != "false" &&
        token.text != "null") {
      return token.text;
    }
  }
  return {};
}

std::optional<ContentLexer::Operation> ContentLexer::next() {
  using Kind = Lexer::TokenKind;
  std::vector<Object> operands;
  std::optional<std::size_t> start;
  bool overflowed = false;
  while (true) {
    const Lexer::Token token = lexer_.next();
    if (token.kind == Kind::end) {
      if (!operands.empty()) {
        ++faults_;
      }
      return std::nullopt;
    }
    if (!start) {
      start = token.start;
    }
    if (token.kind == Kind::keyword && token.text != "true" && token.text != "false" &&
        token.text != "null") {
      if (++count_ > limits_->operators_per_page) {
        limits_->refuse_operators(what_);
      }
      if (token.text == "BI") {
        if (!operands.empty()) {
          ++faults_;
        }
        return inline_image(token.start, token.start);
      }
      Operation operation;
      operation.op = token.text;
      operation.operands = std::move(operands);
      operation.start = *start;
      operation.end = token.end;
      operation.operator_start = token.start;
      if (operation.op == "BX") {
        ++compatibility_;
      } else if (operation.op == "EX" && compatibility_ > 0) {
        --compatibility_;
      } else if (compatibility_ == 0 && !is_operator(operation.op) && !draws_nothing(operation) &&
                 lose("unknown_operator", operation.start, operation.end - operation.start)) {
        losses_.back().fault = what_ + ": the operator '" + operation.op + "' at byte " +
                               std::to_string(token.start) +
                               " is not a content operator; it draws nothing";
      }
      return operation;
    }
    if (token.kind == Kind::stray || token.kind == Kind::array_close ||
        token.kind == Kind::dict_close || token.kind == Kind::brace_open ||
        token.kind == Kind::brace_close) {
      ++faults_;
      continue;
    }
    std::vector<Parser::Recovery> recoveries;
    Object operand = Parser::parse_value(lexer_, token, false, recoveries);
    faults_ += recoveries.size();
    lose_recoveries(recoveries, token.start);
    if (operands.size() < max_operands) {
      operands.push_back(std::move(operand));
    } else {
      ++faults_;
      if (!overflowed) {
        overflowed = true;
        if (lose("unlexable_content", token.start, std::nullopt)) {
          losses_.back().fault = what_ + ": more than " + std::to_string(max_operands) +
                                 " operands precede an operator at byte " +
                                 std::to_string(token.start) + "; the rest are dropped";
        }
      }
    }
  }
}

std::optional<std::size_t> ContentLexer::computed_length(const Object& value) const {
  const Dictionary* dictionary = value.dictionary();
  if (dictionary == nullptr || !filter_names(*dictionary).empty()) {
    return std::nullopt;
  }
  const auto width = entry_of(*dictionary, "W", "Width").integer();
  const auto height = entry_of(*dictionary, "H", "Height").integer();
  if (!width || !height || *width <= 0 || *height <= 0 || *width > max_inline_side ||
      *height > max_inline_side) {
    return std::nullopt;
  }
  const bool mask = entry_of(*dictionary, "IM", "ImageMask").boolean().value_or(false);
  std::int64_t bits =
      mask ? 1 : entry_of(*dictionary, "BPC", "BitsPerComponent").integer().value_or(0);
  std::int64_t components = 0;
  if (mask) {
    components = 1;
  } else {
    const Object& space = entry_of(*dictionary, "CS", "ColorSpace");
    const std::string* name = space.name();
    if (const Object::Array* list = space.array(); list != nullptr && !list->empty()) {
      name = list->front().name();
      if (name != nullptr && (*name == "I" || *name == "Indexed")) {
        components = 1;
      }
    } else if (name != nullptr) {
      if (*name == "G" || *name == "DeviceGray" || *name == "CalGray") {
        components = 1;
      } else if (*name == "RGB" || *name == "DeviceRGB" || *name == "CalRGB") {
        components = 3;
      } else if (*name == "CMYK" || *name == "DeviceCMYK") {
        components = 4;
      }
    }
  }
  if (components == 0 || (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16)) {
    return std::nullopt;
  }
  const std::int64_t row = (*width * bits * components + 7) / 8;
  return static_cast<std::size_t>(row * *height);
}

bool ContentLexer::plausible_after(std::size_t position) const {
  Lexer probe(joined_, position);
  for (int i = 0; i < 3; ++i) {
    const Lexer::Token token = probe.next();
    switch (token.kind) {
      case Lexer::TokenKind::end:
        return true;
      case Lexer::TokenKind::keyword:
        if (!is_operator(token.text) && token.text != "true" && token.text != "false" &&
            token.text != "null") {
          return false;
        }
        break;
      case Lexer::TokenKind::stray:
      case Lexer::TokenKind::brace_open:
      case Lexer::TokenKind::brace_close:
        return false;
      default:
        break;
    }
  }
  return true;
}

ContentLexer::Operation ContentLexer::inline_image(std::size_t start, std::size_t operator_start) {
  using Kind = Lexer::TokenKind;
  Operation operation;
  operation.op = "BI";
  operation.start = start;
  operation.operator_start = operator_start;
  Dictionary dictionary;
  std::optional<Lexer::Token> id;
  // The integer value just read, and whether the generation of a reference
  // followed it, so its R is known for what it is.
  bool integer_value = false;
  bool generation_read = false;
  while (true) {
    const Lexer::Token token = lexer_.next();
    if (token.kind == Kind::end) {
      break;
    }
    if (token.is_keyword("ID")) {
      id = token;
      break;
    }
    if (token.kind != Kind::name) {
      ++faults_;
      // "/Key n g R" written in an inline image's dictionary: the generation
      // and the R stand where keys belong and drop nothing.
      const bool generation = integer_value && token.kind == Kind::integer &&
                              Lexer(joined_, lexer_.position()).next().is_keyword("R");
      const bool reference = generation || (generation_read && token.is_keyword("R"));
      generation_read = generation;
      integer_value = false;
      if (!reference && lose("inline_image", start, std::nullopt)) {
        losses_.back().fault = what_ + ": the inline image at byte " + std::to_string(start) +
                               " holds a dictionary key at byte " + std::to_string(token.start) +
                               " that is not a name";
      }
      continue;
    }
    generation_read = false;
    const Lexer::Token value = lexer_.next();
    if (value.is_keyword("ID")) {
      ++faults_;
      if (lose("inline_image", start, std::nullopt)) {
        losses_.back().fault = what_ + ": the inline image at byte " + std::to_string(start) +
                               " gives /" + token.text + " no value";
      }
      id = value;
      break;
    }
    std::vector<Parser::Recovery> recoveries;
    Object parsed = Parser::parse_value(lexer_, value, false, recoveries);
    integer_value = parsed.integer().has_value();
    dictionary.set(token.text, std::move(parsed));
    faults_ += recoveries.size();
    lose_recoveries(recoveries, value.start);
  }
  operation.operands.push_back(Object::make_dictionary(dictionary));
  if (!id) {
    ++faults_;
    if (lose("inline_image", start, joined_.size() - start)) {
      losses_.back().fault = what_ + ": the inline image at byte " + std::to_string(start) +
                             " has no ID; the rest of the content is read as its dictionary";
    }
    operation.data_start = operation.data_end = operation.end = joined_.size();
    return operation;
  }
  const platform::ByteSpan bytes = joined_;
  std::size_t data_start = id->end;
  // One white-space byte follows ID (ISO 32000-1 section 8.9.7).
  if (data_start < bytes.size() && Lexer::is_white_space(bytes[data_start])) {
    ++data_start;
  }
  operation.data_start = data_start;

  // Whether "EI" follows position after optional white space, as a token;
  // answers its start.
  const auto ei_after = [&](std::size_t position) -> std::optional<std::size_t> {
    std::size_t at = position;
    while (at < bytes.size() && Lexer::is_white_space(bytes[at])) {
      ++at;
    }
    if (find_text(bytes, at, "EI") == at &&
        (at + 2 >= bytes.size() || !Lexer::is_regular(bytes[at + 2]))) {
      return at;
    }
    return std::nullopt;
  };

  std::optional<std::size_t> data_end;
  std::optional<std::size_t> ei;
  const std::vector<std::string> filters = filter_names(dictionary);
  if (const auto length = computed_length(operation.operands.front())) {
    if (*length <= bytes.size() - data_start) {
      if (const auto found = ei_after(data_start + *length)) {
        data_end = data_start + *length;
        ei = found;
      }
    }
  } else if (filters.size() == 1 &&
             (filters.front() == "ASCIIHexDecode" || filters.front() == "ASCII85Decode")) {
    // An ASCII filter's end marker makes the end computable.
    const bool hex = filters.front() == "ASCIIHexDecode";
    if (const auto marker = find_text(bytes, data_start, hex ? ">" : "~>")) {
      const std::size_t after = *marker + (hex ? 1 : 2);
      if (const auto found = ei_after(after)) {
        data_end = after;
        ei = found;
      }
    }
  }
  if (!ei) {
    // The first EI preceded by white space and followed by white space or the
    // end whose following bytes lex as operators (PdfPig's check).
    std::size_t from = data_start;
    while (const auto found = find_text(bytes, from, "EI")) {
      const bool before = *found > data_start && Lexer::is_white_space(bytes[*found - 1]);
      const bool after = *found + 2 >= bytes.size() || Lexer::is_white_space(bytes[*found + 2]);
      if (before && after && plausible_after(*found + 2)) {
        ei = found;
        std::size_t end = *found - 1;
        if (end > data_start && bytes[end - 1] == std::byte{'\r'} &&
            bytes[end] == std::byte{'\n'}) {
          --end;
        }
        data_end = end;
        break;
      }
      from = *found + 1;
    }
  }
  if (!ei) {
    ++faults_;
    if (lose("inline_image", start, bytes.size() - data_start)) {
      losses_.back().fault = what_ + ": the inline image at byte " + std::to_string(start) +
                             " has no EI; the rest of the content is read as its data";
    }
    operation.data_end = operation.end = bytes.size();
    lexer_.seek(bytes.size());
    return operation;
  }
  operation.data_end = std::max(data_end.value_or(data_start), data_start);
  operation.end = *ei + 2;
  lexer_.seek(operation.end);
  return operation;
}

}  // namespace pdfkit::syntax
