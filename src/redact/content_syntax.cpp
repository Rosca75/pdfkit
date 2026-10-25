// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The operand writers. Numbers go through std::to_chars in fixed notation, so
// the locale never enters them (CLAUDE.md section 9).

#include "redact/content_syntax.hpp"

#include <array>
#include <charconv>
#include <cmath>

namespace pdfkit::redact {

std::string ContentSyntax::number(double value) {
  if (!std::isfinite(value)) {
    return "0";
  }
  const double rounded = std::round(value * 10000.0) / 10000.0;
  if (rounded == 0.0) {
    return "0";
  }
  std::array<char, 64> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), rounded,
                                          std::chars_format::fixed, 4);
  if (error != std::errc()) {
    return "0";
  }
  std::string out(buffer.data(), end);
  while (!out.empty() && out.back() == '0') {
    out.pop_back();
  }
  if (!out.empty() && out.back() == '.') {
    out.pop_back();
  }
  return out;
}

std::string ContentSyntax::literal(std::string_view bytes) {
  std::string out = "(";
  constexpr std::string_view octal = "01234567";
  for (const char value : bytes) {
    const auto byte = static_cast<unsigned char>(value);
    if (value == '(' || value == ')' || value == '\\') {
      out += '\\';
      out += value;
    } else if (byte >= 0x20 && byte < 0x7F) {
      out += value;
    } else {
      out += '\\';
      out += octal[(byte >> 6U) & 7U];
      out += octal[(byte >> 3U) & 7U];
      out += octal[byte & 7U];
    }
  }
  out += ')';
  return out;
}

std::string ContentSyntax::hex(std::string_view bytes) {
  constexpr std::string_view digits = "0123456789ABCDEF";
  std::string out = "<";
  for (const char value : bytes) {
    const auto byte = static_cast<unsigned char>(value);
    out += digits[byte >> 4U];
    out += digits[byte & 0x0FU];
  }
  out += '>';
  return out;
}

std::string ContentSyntax::matrix(const text::Matrix& value) {
  return number(value.a) + " " + number(value.b) + " " + number(value.c) + " " +
         number(value.d) + " " + number(value.e) + " " + number(value.f);
}

}  // namespace pdfkit::redact
