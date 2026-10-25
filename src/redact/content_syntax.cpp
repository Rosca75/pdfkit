// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The operand writers. Numbers go through std::to_chars in fixed notation, so
// the locale never enters them (CLAUDE.md section 9).

#include "redact/content_syntax.hpp"

#include <algorithm>
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
      const unsigned code = byte;
      out += octal[(code >> 6U) & 7U];
      out += octal[(code >> 3U) & 7U];
      out += octal[code & 7U];
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

std::vector<std::pair<std::size_t, std::size_t>> ContentSyntax::gaps(
    std::span<const std::byte> bytes, std::span<const syntax::ContentLexer::Operation> operations) {
  std::vector<std::pair<std::size_t, std::size_t>> out;
  // The white space of ISO 32000-1 table 1 (read).
  const auto white = [&bytes](std::size_t at) {
    const auto value = std::to_integer<unsigned>(bytes[at]);
    return value == 0 || value == 9 || value == 10 || value == 12 || value == 13 || value == 32;
  };
  const auto gap = [&](std::size_t from, std::size_t to) {
    to = std::min(to, bytes.size());
    while (from < to && white(from)) {
      ++from;
    }
    while (to > from && white(to - 1)) {
      --to;
    }
    if (from < to) {
      out.emplace_back(from, to);
    }
  };
  std::size_t cursor = 0;
  for (const syntax::ContentLexer::Operation& operation : operations) {
    gap(cursor, operation.start);
    cursor = std::max(cursor, operation.end);
  }
  gap(cursor, bytes.size());
  return out;
}

std::string ContentSyntax::matrix(const text::Matrix& value) {
  return number(value.a) + " " + number(value.b) + " " + number(value.c) + " " + number(value.d) +
         " " + number(value.e) + " " + number(value.f);
}

}  // namespace pdfkit::redact
