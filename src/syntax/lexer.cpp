// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The token readers of the lexer. Numbers are parsed by hand for integers and
// with std::from_chars for reals, so the result never depends on the locale.

#include "syntax/lexer.hpp"

#include <charconv>
#include <limits>
#include <system_error>

namespace pdfkit::syntax {

namespace {

constexpr unsigned cr = 0x0D;
constexpr unsigned lf = 0x0A;

[[nodiscard]] int hex_value(unsigned value) noexcept {
  if (value >= '0' && value <= '9') {
    return static_cast<int>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<int>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<int>(value - 'A' + 10);
  }
  return -1;
}

[[nodiscard]] bool is_digit(unsigned value) noexcept {
  return value >= '0' && value <= '9';
}

}  // namespace

Lexer::Lexer(platform::ByteSpan bytes, std::size_t position) noexcept
    : bytes_(bytes), position_(position < bytes.size() ? position : bytes.size()) {}

void Lexer::seek(std::size_t position) noexcept {
  position_ = position < bytes_.size() ? position : bytes_.size();
}

bool Lexer::is_white_space(std::byte value) noexcept {
  switch (std::to_integer<unsigned>(value)) {
    case 0x00:
    case 0x09:
    case 0x0A:
    case 0x0C:
    case 0x0D:
    case 0x20:
      return true;
    default:
      return false;
  }
}

bool Lexer::is_delimiter(std::byte value) noexcept {
  switch (std::to_integer<unsigned>(value)) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '[':
    case ']':
    case '{':
    case '}':
    case '/':
    case '%':
      return true;
    default:
      return false;
  }
}

bool Lexer::is_regular(std::byte value) noexcept {
  return !is_white_space(value) && !is_delimiter(value);
}

unsigned Lexer::peek_byte(std::size_t ahead) const noexcept {
  const std::size_t at = position_ + ahead;
  return at < bytes_.size() ? std::to_integer<unsigned>(bytes_[at]) : 0x100U;
}

void Lexer::skip_white_space() noexcept {
  while (!at_end()) {
    const std::byte value = bytes_[position_];
    if (is_white_space(value)) {
      ++position_;
    } else if (std::to_integer<unsigned>(value) == '%') {
      while (!at_end() && peek_byte() != cr && peek_byte() != lf) {
        ++position_;
      }
    } else {
      return;
    }
  }
}

Lexer::Token Lexer::next() {
  skip_white_space();
  const std::size_t start = position_;
  if (at_end()) {
    return Token{TokenKind::end, start, start, 0, 0.0, {}, false};
  }
  const unsigned first = peek_byte();
  const auto single = [&](TokenKind kind, std::size_t length) {
    position_ += length;
    return Token{kind, start, position_, 0, 0.0, {}, false};
  };
  switch (first) {
    case '(':
      return read_literal_string(start);
    case ')':
      return single(TokenKind::stray, 1);
    case '<':
      if (peek_byte(1) == '<') {
        return single(TokenKind::dict_open, 2);
      }
      return read_hex_string(start);
    case '>':
      if (peek_byte(1) == '>') {
        return single(TokenKind::dict_close, 2);
      }
      return single(TokenKind::stray, 1);
    case '[':
      return single(TokenKind::array_open, 1);
    case ']':
      return single(TokenKind::array_close, 1);
    case '{':
      return single(TokenKind::brace_open, 1);
    case '}':
      return single(TokenKind::brace_close, 1);
    case '/':
      return read_name(start);
    default:
      break;
  }
  if (is_digit(first) || first == '+' || first == '-' || first == '.') {
    return read_number(start);
  }
  return read_keyword(start);
}

Lexer::Token Lexer::read_number(std::size_t start) {
  Token token{TokenKind::integer, start, start, 0, 0.0, {}, false};
  bool negative = false;
  std::size_t signs = 0;
  while (peek_byte() == '+' || peek_byte() == '-') {
    // pdf.js reads "--5" as -5 (recalled); the first sign decides.
    if (signs == 0) {
      negative = peek_byte() == '-';
    }
    ++signs;
    ++position_;
  }
  if (signs > 1) {
    token.repaired = true;
  }
  const std::size_t digits_start = position_;
  bool seen_point = false;
  bool seen_digit = false;
  while (!at_end()) {
    const unsigned value = peek_byte();
    if (is_digit(value)) {
      seen_digit = true;
    } else if (value == '.' && !seen_point) {
      seen_point = true;
    } else {
      break;
    }
    ++position_;
  }
  token.end = position_;
  if (!seen_digit) {
    // A sign or a point alone reads as 0, as PDFium and pdf.js read it (recalled).
    token.repaired = true;
    return token;
  }
  const char* first = reinterpret_cast<const char*>(bytes_.data() + digits_start);
  const char* last = reinterpret_cast<const char*>(bytes_.data() + position_);
  if (!seen_point) {
    // Accumulate as a negative value so INT64_MIN fits.
    std::int64_t value = 0;
    bool overflow = false;
    for (const char* at = first; at != last; ++at) {
      const auto digit = static_cast<std::int64_t>(*at - '0');
      if (value < (std::numeric_limits<std::int64_t>::min() + digit) / 10) {
        overflow = true;
        break;
      }
      value = value * 10 - digit;
    }
    if (!overflow && !negative && value == std::numeric_limits<std::int64_t>::min()) {
      overflow = true;
    }
    if (!overflow) {
      token.integer = negative ? value : -value;
      return token;
    }
  }
  token.kind = TokenKind::real;
  double magnitude = 0.0;
  // "5." and ".5" are valid PDF reals; from_chars reads both in the general format.
  const std::from_chars_result parsed =
      std::from_chars(first, last, magnitude, std::chars_format::fixed);
  if (parsed.ec == std::errc::result_out_of_range) {
    magnitude = std::numeric_limits<double>::max();
  } else if (parsed.ec != std::errc{}) {
    magnitude = 0.0;
    token.repaired = true;
  }
  token.real = negative ? -magnitude : magnitude;
  return token;
}

Lexer::Token Lexer::read_literal_string(std::size_t start) {
  Token token{TokenKind::literal_string, start, start, 0, 0.0, {}, false};
  ++position_;  // (
  int depth = 1;
  while (true) {
    if (at_end()) {
      token.repaired = true;
      break;
    }
    const unsigned value = peek_byte();
    ++position_;
    if (value == '(') {
      ++depth;
      token.text.push_back('(');
    } else if (value == ')') {
      if (--depth == 0) {
        break;
      }
      token.text.push_back(')');
    } else if (value == cr) {
      // An unescaped CR, LF or CR LF reads as one LF (ISO 32000-1 section 7.3.4.2).
      if (peek_byte() == lf) {
        ++position_;
      }
      token.text.push_back('\n');
    } else if (value == '\\') {
      if (at_end()) {
        token.repaired = true;
        break;
      }
      const unsigned escaped = peek_byte();
      ++position_;
      switch (escaped) {
        case 'n':
          token.text.push_back('\n');
          break;
        case 'r':
          token.text.push_back('\r');
          break;
        case 't':
          token.text.push_back('\t');
          break;
        case 'b':
          token.text.push_back('\b');
          break;
        case 'f':
          token.text.push_back('\f');
          break;
        case cr:
          // A line continuation contributes nothing.
          if (peek_byte() == lf) {
            ++position_;
          }
          break;
        case lf:
          break;
        default:
          if (escaped >= '0' && escaped <= '7') {
            unsigned code = escaped - '0';
            for (int more = 0; more < 2 && peek_byte() >= '0' && peek_byte() <= '7'; ++more) {
              code = code * 8 + (peek_byte() - '0');
              ++position_;
            }
            // Above \377 the high-order overflow is ignored (ISO 32000-1 section 7.3.4.2).
            token.text.push_back(static_cast<char>(code & 0xFFU));
          } else {
            // An unknown escape drops the backslash (ISO 32000-1 section 7.3.4.2).
            token.text.push_back(static_cast<char>(escaped));
          }
          break;
      }
    } else {
      token.text.push_back(static_cast<char>(value));
    }
  }
  token.end = position_;
  return token;
}

Lexer::Token Lexer::read_hex_string(std::size_t start) {
  Token token{TokenKind::hex_string, start, start, 0, 0.0, {}, false};
  ++position_;       // <
  int pending = -1;  // the first digit of a pair, or -1
  while (true) {
    if (at_end()) {
      token.repaired = true;
      break;
    }
    const unsigned value = peek_byte();
    if (value == '>') {
      ++position_;
      break;
    }
    if (is_white_space(bytes_[position_])) {
      ++position_;
      continue;
    }
    const int digit = hex_value(value);
    if (digit < 0) {
      // A byte that is not a hexadecimal digit ends the string.
      token.repaired = true;
      break;
    }
    ++position_;
    if (pending < 0) {
      pending = digit;
    } else {
      token.text.push_back(
          static_cast<char>((static_cast<unsigned>(pending) << 4U) | static_cast<unsigned>(digit)));
      pending = -1;
    }
  }
  if (pending >= 0) {
    // An odd final digit is followed by an assumed 0 (ISO 32000-1 section 7.3.4.3).
    token.text.push_back(static_cast<char>(static_cast<unsigned>(pending) << 4U));
  }
  token.end = position_;
  return token;
}

Lexer::Token Lexer::read_name(std::size_t start) {
  Token token{TokenKind::name, start, start, 0, 0.0, {}, false};
  ++position_;  // /
  while (!at_end() && is_regular(bytes_[position_])) {
    const unsigned value = peek_byte();
    if (value == '#') {
      const int high = hex_value(peek_byte(1));
      const int low = hex_value(peek_byte(2));
      if (high >= 0 && low >= 0 && is_regular(bytes_[position_ + 1]) &&
          is_regular(bytes_[position_ + 2])) {
        token.text.push_back(
            static_cast<char>((static_cast<unsigned>(high) << 4U) | static_cast<unsigned>(low)));
        position_ += 3;
        continue;
      }
      // A '#' without two hexadecimal digits is kept as written, as pdf.js keeps it
      // (recalled).
      token.repaired = true;
    }
    token.text.push_back(static_cast<char>(value));
    ++position_;
  }
  token.end = position_;
  return token;
}

Lexer::Token Lexer::read_keyword(std::size_t start) {
  Token token{TokenKind::keyword, start, start, 0, 0.0, {}, false};
  while (!at_end() && is_regular(bytes_[position_])) {
    token.text.push_back(static_cast<char>(peek_byte()));
    ++position_;
  }
  if (position_ == start) {
    // Unreachable for a well-formed walk; one byte is consumed so the walk
    // always advances.
    token.text.push_back(static_cast<char>(peek_byte()));
    ++position_;
  }
  token.end = position_;
  return token;
}

}  // namespace pdfkit::syntax
