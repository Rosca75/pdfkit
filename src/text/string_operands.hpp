// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// StringOperands maps every byte of a content operator's string operands back
// to the bytes that wrote it in the stream, so a glyph record can name the
// exact bytes of its code (CLAUDE.md section 4) and a redaction can cut there.
// A hexadecimal string writes a byte as two digits (white space between
// digits belongs to no byte; an odd final digit is a byte by itself, with an
// assumed 0); a literal string writes a byte as itself, as an escape (\n, \(,
// \\, \ddd octal, or a backslash before any other character, which is
// dropped), or as an unescaped end of line (CR, LF or CR LF reading as one
// LF), and a line continuation (backslash and end of line) writes nothing,
// all by ISO 32000-1 section 7.3.4 (read) and exactly as syntax::Lexer
// decodes them. The scan reads the operands' tokens again from the stream,
// because the content lexer keeps operand values, not token positions.

#ifndef PDFKIT_TEXT_STRING_OPERANDS_HPP
#define PDFKIT_TEXT_STRING_OPERANDS_HPP

#include "platform/byte_span.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::text {

class StringOperands {
 public:
  // One string token among the operands.
  struct Token {
    bool hex = false;
    // [start, end) of the whole token, delimiters included.
    std::size_t start = 0;
    std::size_t end = 0;
    // The decoded bytes, and for each the [first, second) of the bytes that
    // wrote it.
    std::string bytes;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
  };

  // The string tokens between start and end of bytes (an operation's
  // operands), in order, nested in arrays or not.
  [[nodiscard]] static std::vector<Token> scan(platform::ByteSpan bytes, std::size_t start,
                                               std::size_t end);

  // What a piece of a string's encoded form (the bytes between two code
  // boundaries) decodes to.
  [[nodiscard]] static std::string decode_fragment(platform::ByteSpan fragment, bool hex);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_STRING_OPERANDS_HPP
