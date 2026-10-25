// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Lexer is the byte walk of docs/DESIGN.md section 1.1 over ISO 32000-1
// section 7.2 (read): white space, comments, delimiters, numbers, literal and
// hexadecimal strings, names and keywords, each token with its byte range,
// because reconstruction and the writer read positions. The leniencies every
// robust reader shares are applied here and named where they come from: a
// doubled sign and a sign alone (pdf.js, recalled), a lone '#' in a name kept
// as written (pdf.js, recalled), an unterminated string ending at the end of
// the data, a non-hexadecimal byte ending a hexadecimal string. A token read
// through such a leniency is marked repaired, so the caller can record it.
// The content-stream lexer and the object parser share this walk.

#ifndef PDFKIT_SYNTAX_LEXER_HPP
#define PDFKIT_SYNTAX_LEXER_HPP

#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pdfkit::syntax {

class Lexer {
 public:
  enum class TokenKind : std::uint8_t {
    end,             // no byte left
    integer,         // a number written without a decimal point that fits 64 bits
    real,            // any other number
    literal_string,  // (...)
    hex_string,      // <...>
    name,            // /Name, with #xx decoded
    keyword,         // a run of regular bytes that is not a number: true, obj, Tj ...
    array_open,      // [
    array_close,     // ]
    dict_open,       // <<
    dict_close,      // >>
    brace_open,      // {
    brace_close,     // }
    stray,           // a delimiter out of place: ')' or a lone '>'
  };

  struct Token {
    TokenKind kind = TokenKind::end;
    // The token's bytes are [start, end) of the lexer's span.
    std::size_t start = 0;
    std::size_t end = 0;
    std::int64_t integer = 0;
    double real = 0.0;
    // The decoded bytes of a string or a name, the text of a keyword.
    std::string text;
    // The token was read through a leniency of docs/DESIGN.md section 1.1.
    bool repaired = false;

    [[nodiscard]] bool is_number() const noexcept {
      return kind == TokenKind::integer || kind == TokenKind::real;
    }
    [[nodiscard]] bool is_keyword(std::string_view word) const noexcept {
      return kind == TokenKind::keyword && text == word;
    }
    // The value of a number token.
    [[nodiscard]] double number() const noexcept {
      return kind == TokenKind::integer ? static_cast<double>(integer) : real;
    }
  };

  explicit Lexer(platform::ByteSpan bytes, std::size_t position = 0) noexcept;

  // The next token; TokenKind::end at the end of the data.
  [[nodiscard]] Token next();

  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  void seek(std::size_t position) noexcept;
  [[nodiscard]] platform::ByteSpan bytes() const noexcept { return bytes_; }

  // Skips white space and comments; the position is then at a token's first
  // byte or at the end.
  void skip_white_space() noexcept;

  // The character classes of ISO 32000-1 section 7.2.2, tables 1 and 2 (read).
  [[nodiscard]] static bool is_white_space(std::byte value) noexcept;
  [[nodiscard]] static bool is_delimiter(std::byte value) noexcept;
  [[nodiscard]] static bool is_regular(std::byte value) noexcept;

 private:
  Token read_number(std::size_t start);
  Token read_literal_string(std::size_t start);
  Token read_hex_string(std::size_t start);
  Token read_name(std::size_t start);
  Token read_keyword(std::size_t start);

  [[nodiscard]] bool at_end() const noexcept { return position_ >= bytes_.size(); }
  [[nodiscard]] unsigned peek_byte(std::size_t ahead = 0) const noexcept;

  platform::ByteSpan bytes_;
  std::size_t position_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_LEXER_HPP
