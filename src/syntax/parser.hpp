// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Parser reads objects from the tokens of the lexer, docs/DESIGN.md section
// 1.3: direct objects, "n g R" recognised by a two-token lookahead after an
// integer, and indirect objects "n g obj ... endobj" at an offset. A stream's
// /Length is trusted only when "endstream" follows the data it delimits after
// optional white space; otherwise the data ends at the next "endstream" found
// by scanning forward, less the end of line before it, which is the recovery
// of pdf.js's parser.js and of QPDF (read in the design record). A missing
// endobj ends the object where the next token starts. Every recovery is a
// reason the caller records. The parser never follows a reference: an
// indirect /Length is answered by the caller, which owns resolution and its
// cycle guard.

#ifndef PDFKIT_SYNTAX_PARSER_HPP
#define PDFKIT_SYNTAX_PARSER_HPP

#include "platform/byte_span.hpp"
#include "syntax/lexer.hpp"
#include "syntax/object.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::syntax {

class Parser {
 public:
  // The value of an indirect /Length, or no value when it cannot be had.
  using LengthResolver = std::function<std::optional<std::int64_t>(Object::Reference)>;

  struct Indirect {
    Object::Reference reference;
    Object object;
    // The first byte of "n g obj" and the byte after the object's end.
    std::size_t start = 0;
    std::size_t end = 0;
    // Each recovery applied, in the words a repair report uses.
    std::vector<std::string> repairs;
  };

  // Containers nested deeper than this end there, so a hostile file cannot
  // exhaust the stack.
  static constexpr int max_depth = 256;

  explicit Parser(platform::ByteSpan bytes) noexcept : bytes_(bytes) {}

  // Parses the object at offset when the bytes there read "n g obj"; no value
  // otherwise. resolve_length may be empty, in which case an indirect /Length
  // is unknown.
  [[nodiscard]] std::optional<Indirect> parse_indirect(std::size_t offset,
                                                       const LengthResolver& resolve_length) const;

  // Reads the header "n g obj" at offset: the reference and the position
  // after "obj", or no value.
  [[nodiscard]] std::optional<std::pair<Object::Reference, std::size_t>> read_header(
      std::size_t offset) const;

  // Parses one direct object starting with first, the token the lexer just
  // read. With references off ("n g R" is not looked for) this is the operand
  // syntax of a content stream. A recovery appends to repairs.
  [[nodiscard]] static Object parse_value(Lexer& lexer, const Lexer::Token& first, bool references,
                                          std::vector<std::string>& repairs, int depth = 0);

  // Parses one direct object at offset (a trailer dictionary, a test's text).
  [[nodiscard]] Object parse_direct(std::size_t offset, std::vector<std::string>& repairs) const;

  // The first occurrence of word at or after from, as a token (preceded and
  // followed by a byte that is not regular, or the edge); no value when none.
  [[nodiscard]] static std::optional<std::size_t> find_keyword(platform::ByteSpan bytes,
                                                               std::size_t from,
                                                               std::string_view word);

 private:
  platform::ByteSpan bytes_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_PARSER_HPP
