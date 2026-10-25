// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The byte-to-source map of string operands. The walk below repeats the
// decoding rules of syntax::Lexer step by step, recording where each byte came
// from; scan() checks its result against the lexer's own decoding of the
// token and leaves a token's ranges empty when the two disagree, so a
// disagreement can never place a code on the wrong bytes.

#include "text/string_operands.hpp"

#include "syntax/lexer.hpp"

namespace pdfkit::text {

namespace {

constexpr unsigned cr = '\r';
constexpr unsigned lf = '\n';

[[nodiscard]] unsigned at(platform::ByteSpan bytes, std::size_t index) noexcept {
  return index < bytes.size() ? std::to_integer<unsigned>(bytes[index]) : 0U;
}

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

// Decodes a literal string's body from position, recording each byte's
// source; nested stops at the closing parenthesis of depth zero when true.
void literal(platform::ByteSpan bytes, std::size_t position, std::size_t end, bool nested,
             std::string& out, std::vector<std::pair<std::size_t, std::size_t>>* ranges) {
  int depth = 1;
  while (position < end) {
    const std::size_t first = position;
    const unsigned value = at(bytes, position);
    ++position;
    char produced = 0;
    bool produces = true;
    if (value == '(') {
      ++depth;
      produced = '(';
    } else if (value == ')') {
      if (nested && --depth == 0) {
        return;
      }
      produced = ')';
    } else if (value == cr) {
      if (position < end && at(bytes, position) == lf) {
        ++position;
      }
      produced = '\n';
    } else if (value == '\\') {
      if (position >= end) {
        return;
      }
      const unsigned escaped = at(bytes, position);
      ++position;
      switch (escaped) {
        case 'n':
          produced = '\n';
          break;
        case 'r':
          produced = '\r';
          break;
        case 't':
          produced = '\t';
          break;
        case 'b':
          produced = '\b';
          break;
        case 'f':
          produced = '\f';
          break;
        case cr:
          if (position < end && at(bytes, position) == lf) {
            ++position;
          }
          produces = false;
          break;
        case lf:
          produces = false;
          break;
        default:
          if (escaped >= '0' && escaped <= '7') {
            unsigned code = escaped - '0';
            for (int more = 0; more < 2 && position < end && at(bytes, position) >= '0' &&
                               at(bytes, position) <= '7';
                 ++more) {
              code = (code * 8) + (at(bytes, position) - '0');
              ++position;
            }
            produced = static_cast<char>(code & 0xFFU);
          } else {
            produced = static_cast<char>(escaped);
          }
          break;
      }
    } else {
      produced = static_cast<char>(value);
    }
    if (produces) {
      out.push_back(produced);
      if (ranges != nullptr) {
        ranges->emplace_back(first, position);
      }
    }
  }
}

// Decodes a hexadecimal string's body from position up to '>' or end.
void hexadecimal(platform::ByteSpan bytes, std::size_t position, std::size_t end, std::string& out,
                 std::vector<std::pair<std::size_t, std::size_t>>* ranges) {
  int pending = -1;
  std::size_t pending_at = 0;
  while (position < end) {
    const unsigned value = at(bytes, position);
    if (value == '>') {
      break;
    }
    if (syntax::Lexer::is_white_space(bytes[position])) {
      ++position;
      continue;
    }
    const int digit = hex_value(value);
    if (digit < 0) {
      break;
    }
    if (pending < 0) {
      pending = digit;
      pending_at = position;
    } else {
      out.push_back(
          static_cast<char>((static_cast<unsigned>(pending) << 4U) | static_cast<unsigned>(digit)));
      if (ranges != nullptr) {
        ranges->emplace_back(pending_at, position + 1);
      }
      pending = -1;
    }
    ++position;
  }
  if (pending >= 0) {
    out.push_back(static_cast<char>(static_cast<unsigned>(pending) << 4U));
    if (ranges != nullptr) {
      ranges->emplace_back(pending_at, pending_at + 1);
    }
  }
}

}  // namespace

std::vector<StringOperands::Token> StringOperands::scan(platform::ByteSpan bytes, std::size_t start,
                                                        std::size_t end) {
  std::vector<Token> tokens;
  syntax::Lexer lexer(bytes, start);
  while (true) {
    const syntax::Lexer::Token token = lexer.next();
    if (token.kind == syntax::Lexer::TokenKind::end || token.start >= end) {
      break;
    }
    const bool is_hex = token.kind == syntax::Lexer::TokenKind::hex_string;
    if (!is_hex && token.kind != syntax::Lexer::TokenKind::literal_string) {
      continue;
    }
    Token out;
    out.hex = is_hex;
    out.start = token.start;
    out.end = token.end;
    if (is_hex) {
      hexadecimal(bytes, token.start + 1, token.end, out.bytes, &out.ranges);
    } else {
      literal(bytes, token.start + 1, token.end, true, out.bytes, &out.ranges);
    }
    if (out.bytes != token.text) {
      out.bytes = token.text;
      out.ranges.clear();
    }
    tokens.push_back(std::move(out));
  }
  return tokens;
}

std::string StringOperands::decode_fragment(platform::ByteSpan fragment, bool hex) {
  std::string out;
  if (hex) {
    hexadecimal(fragment, 0, fragment.size(), out, nullptr);
  } else {
    literal(fragment, 0, fragment.size(), false, out, nullptr);
  }
  return out;
}

}  // namespace pdfkit::text
