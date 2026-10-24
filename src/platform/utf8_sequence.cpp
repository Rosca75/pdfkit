// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The decoder walks table 3-7 of the Unicode Standard (recalled): the lead byte
// fixes the length and the range of the second byte; every later byte is a
// plain continuation byte.

#include "platform/utf8_sequence.hpp"

#include <cstdint>

namespace pdfkit::platform {

namespace {

std::uint8_t byte_at(std::string_view text, std::size_t at) noexcept {
  return static_cast<std::uint8_t>(text[at]);
}

bool in_range(std::uint8_t value, std::uint8_t low, std::uint8_t high) noexcept {
  return value >= low && value <= high;
}

}  // namespace

Utf8Sequence next_utf8_sequence(std::string_view text, std::size_t at) noexcept {
  const std::uint8_t lead = byte_at(text, at);
  if (lead < 0x80U) {
    return {1, lead, true};
  }
  std::size_t length = 0;
  std::uint8_t second_low = 0x80U;
  std::uint8_t second_high = 0xBFU;
  char32_t code_point = 0;
  if (in_range(lead, 0xC2U, 0xDFU)) {
    length = 2;
    code_point = lead & 0x1FU;
  } else if (in_range(lead, 0xE0U, 0xEFU)) {
    length = 3;
    code_point = lead & 0x0FU;
    if (lead == 0xE0U) {
      second_low = 0xA0U;
    } else if (lead == 0xEDU) {
      second_high = 0x9FU;
    }
  } else if (in_range(lead, 0xF0U, 0xF4U)) {
    length = 4;
    code_point = lead & 0x07U;
    if (lead == 0xF0U) {
      second_low = 0x90U;
    } else if (lead == 0xF4U) {
      second_high = 0x8FU;
    }
  } else {
    return {1, 0, false};
  }
  for (std::size_t i = 1; i < length; ++i) {
    if (at + i >= text.size()) {
      return {i, 0, false};
    }
    const std::uint8_t next = byte_at(text, at + i);
    const bool fits =
        i == 1 ? in_range(next, second_low, second_high) : in_range(next, 0x80U, 0xBFU);
    if (!fits) {
      return {i, 0, false};
    }
    code_point = (code_point << 6U) | (next & 0x3FU);
  }
  return {length, code_point, true};
}

void append_utf8(std::string& out, char32_t code_point) {
  if (code_point < 0x80U) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point < 0x800U) {
    out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point < 0x10000U) {
    out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
}

}  // namespace pdfkit::platform
