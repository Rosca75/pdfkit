// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ContentSyntax writes the operands the redaction puts into a content stream,
// in the syntax of ISO 32000-1 section 7.3 (read): a number with at most four
// decimals and no exponent, so it reads back within 0.0001 of its value
// whatever the reader (a TJ number of thousandths of text space then moves
// the following glyphs by under a millionth of the font size); a string as a
// literal with "(", ")", "\" and every byte outside printable ASCII escaped,
// or as hexadecimal; a matrix as six numbers. Every form is a function of
// the value alone, so one edit gives one byte sequence.

#ifndef PDFKIT_REDACT_CONTENT_SYNTAX_HPP
#define PDFKIT_REDACT_CONTENT_SYNTAX_HPP

#include "syntax/content_lexer.hpp"
#include "text/matrix.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::redact {

class ContentSyntax {
 public:
  // value rounded to four decimals, trailing zeros dropped; a value that is
  // not finite, or rounds to zero, writes "0".
  [[nodiscard]] static std::string number(double value);
  // bytes as a literal string, delimiters included.
  [[nodiscard]] static std::string literal(std::string_view bytes);
  // bytes as a hexadecimal string, delimiters included, upper-case digits.
  [[nodiscard]] static std::string hex(std::string_view bytes);
  // The six numbers of a matrix separated by spaces.
  [[nodiscard]] static std::string matrix(const text::Matrix& value);

  // The bytes of a content stream outside its operations: each [start, end)
  // of the stretches between operations (and after the last) that are not
  // white space. They are comments (ISO 32000-1 section 7.2.3, read) and the
  // bytes a lexer recovery skipped (a stray delimiter, an unterminated
  // string), which no operator draws and which still sit in the file.
  [[nodiscard]] static std::vector<std::pair<std::size_t, std::size_t>> gaps(
      std::span<const std::byte> bytes,
      std::span<const syntax::ContentLexer::Operation> operations);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_CONTENT_SYNTAX_HPP
