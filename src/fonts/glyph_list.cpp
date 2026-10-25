// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The lookup is a binary search over the generated tables, which are sorted
// by the names' bytes. The conventions follow the Adobe Glyph List
// Specification's mapping of a name to a character string (components split
// at underscores, a suffix after the first period dropped, "uni" groups of
// four upper-case hexadecimal digits, "u" with four to six), as pdf.js
// src/core/unicode.js getUnicodeForGlyph applies them (read); a "uni" group in
// lower-case hexadecimal is accepted too, because producers write it.

#include "fonts/glyph_list.hpp"

#include "fonts/generated/glyph_list_table.hpp"
#include "platform/utf8_sequence.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace pdfkit::fonts {

namespace {

using generated::GlyphListTable;

const GlyphListTable::Entry* find(std::span<const GlyphListTable::Entry> table,
                                  std::string_view name) noexcept {
  const auto found = std::lower_bound(
      table.begin(), table.end(), name,
      [](const GlyphListTable::Entry& entry, std::string_view key) { return entry.name < key; });
  if (found != table.end() && found->name == name) {
    return &*found;
  }
  return nullptr;
}

std::optional<std::uint32_t> hex_value(std::string_view digits, bool upper_only) noexcept {
  if (digits.empty() || digits.size() > 8) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  for (const char c : digits) {
    std::uint32_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<std::uint32_t>(c - '0');
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<std::uint32_t>(c - 'A' + 10);
    } else if (!upper_only && c >= 'a' && c <= 'f') {
      digit = static_cast<std::uint32_t>(c - 'a' + 10);
    } else {
      return std::nullopt;
    }
    value = value * 16U + digit;
  }
  return value;
}

std::optional<std::uint32_t> decimal_value(std::string_view digits) noexcept {
  if (digits.empty() || digits.size() > 9) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10U + static_cast<std::uint32_t>(c - '0');
  }
  return value;
}

bool is_surrogate(std::uint32_t value) noexcept {
  return value >= 0xD800 && value <= 0xDFFF;
}

// The code points of one component (no underscore, no period): the lists,
// then "uniXXXX[XXXX...]", then "uXXXX" to "uXXXXXX".
std::optional<std::vector<char32_t>> component(std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }
  const GlyphListTable::Entry* entry = find(GlyphListTable::adobe_glyph_list(), name);
  if (entry == nullptr) {
    entry = find(GlyphListTable::zapf_dingbats(), name);
  }
  if (entry != nullptr) {
    return std::vector<char32_t>(entry->text.begin(), entry->text.end());
  }
  if (name.size() >= 7 && name.substr(0, 3) == "uni" && (name.size() - 3) % 4 == 0) {
    std::vector<char32_t> out;
    for (std::size_t at = 3; at < name.size(); at += 4) {
      const std::optional<std::uint32_t> value = hex_value(name.substr(at, 4), false);
      if (!value || is_surrogate(*value)) {
        return std::nullopt;
      }
      out.push_back(static_cast<char32_t>(*value));
    }
    return out;
  }
  if (name.size() >= 5 && name.size() <= 7 && name[0] == 'u') {
    const std::optional<std::uint32_t> value = hex_value(name.substr(1), true);
    if (value && !is_surrogate(*value) && *value <= 0x10FFFF) {
      return std::vector<char32_t>{static_cast<char32_t>(*value)};
    }
  }
  return std::nullopt;
}

}  // namespace

bool GlyphList::usable(char32_t code_point) noexcept {
  const auto value = static_cast<std::uint32_t>(code_point);
  if (value == 0x09) {
    return true;
  }
  if (value < 0x20 || (value >= 0x7F && value < 0xA0)) {
    return false;
  }
  if (is_surrogate(value) || value > 0x10FFFF) {
    return false;
  }
  // The private-use areas: U+E000 to U+F8FF and planes 15 and 16.
  if ((value >= 0xE000 && value <= 0xF8FF) || value >= 0xF0000) {
    return false;
  }
  // The noncharacters U+FDD0 to U+FDEF and the last two of every plane; the
  // replacement character, which says only that something was not read.
  if ((value >= 0xFDD0 && value <= 0xFDEF) || (value & 0xFFFEU) == 0xFFFEU || value == 0xFFFD) {
    return false;
  }
  return true;
}

std::optional<std::string> GlyphList::lookup(std::string_view name) {
  const GlyphListTable::Entry* entry = find(GlyphListTable::adobe_glyph_list(), name);
  if (entry == nullptr) {
    entry = find(GlyphListTable::zapf_dingbats(), name);
  }
  if (entry == nullptr) {
    return std::nullopt;
  }
  std::string out;
  for (const char32_t code_point : entry->text) {
    platform::append_utf8(out, code_point);
  }
  return out;
}

std::optional<std::string> GlyphList::to_unicode(std::string_view name) {
  // A suffix after the first period is a variant tag ("a.sc", "one.oldstyle").
  if (const std::size_t period = name.find('.'); period != std::string_view::npos) {
    if (period == 0) {
      return std::nullopt;
    }
    name = name.substr(0, period);
  }
  std::string out;
  std::size_t start = 0;
  while (start <= name.size()) {
    std::size_t end = name.find('_', start);
    if (end == std::string_view::npos) {
      end = name.size();
    }
    const std::optional<std::vector<char32_t>> points = component(name.substr(start, end - start));
    if (!points) {
      return std::nullopt;
    }
    for (const char32_t code_point : *points) {
      if (!usable(code_point)) {
        return std::nullopt;
      }
      platform::append_utf8(out, code_point);
    }
    start = end + 1;
  }
  if (out.empty()) {
    return std::nullopt;
  }
  return out;
}

std::optional<GlyphList::Number> GlyphList::number(std::string_view name) noexcept {
  const auto read = [](std::string_view digits) -> std::optional<std::uint32_t> {
    if (const std::optional<std::uint32_t> value = decimal_value(digits)) {
      return value;
    }
    return hex_value(digits, false);
  };
  const auto make = [](Number::Kind kind,
                       std::optional<std::uint32_t> value) -> std::optional<Number> {
    if (!value) {
      return std::nullopt;
    }
    return Number{kind, *value};
  };
  if (name.size() > 5 && name.substr(0, 5) == "glyph") {
    return make(Number::Kind::glyph, decimal_value(name.substr(5)));
  }
  if (name.size() > 3 && name.substr(0, 3) == "cid") {
    return make(Number::Kind::cid, decimal_value(name.substr(3)));
  }
  if (name.size() > 1 && (name[0] == 'g' || name[0] == 'G')) {
    return make(Number::Kind::glyph, read(name.substr(1)));
  }
  if (name.size() > 1 && (name[0] == 'C' || name[0] == 'c')) {
    return make(Number::Kind::code, read(name.substr(1)));
  }
  return std::nullopt;
}

}  // namespace pdfkit::fonts
