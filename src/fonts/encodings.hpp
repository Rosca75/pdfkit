// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Encodings holds the simple-font encodings of ISO 32000-1 Annex D (read) as
// tables of 256 glyph names, docs/DESIGN.md section 5.2: Standard, WinAnsi
// (its unassigned codes other than the control range read as bullet, as
// Acrobat shows them), MacRoman, MacExpert and PDFDoc, the built-in encodings
// of Symbol and ZapfDingbats, and the predefined Expert encoding a CFF program
// may name. An empty name is a code the encoding leaves undefined.

#ifndef PDFKIT_FONTS_ENCODINGS_HPP
#define PDFKIT_FONTS_ENCODINGS_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pdfkit::fonts {

class Encodings {
 public:
  enum class Base : std::uint8_t {
    standard,
    win_ansi,
    mac_roman,
    mac_expert,
    pdf_doc,
    symbol,
    zapf_dingbats,
    cff_expert,
  };

  using Table = std::array<std::string_view, 256>;

  // The 256 glyph names of an encoding.
  [[nodiscard]] static const Table& table(Base base) noexcept;

  // The encoding an /Encoding or /BaseEncoding name selects: StandardEncoding,
  // WinAnsiEncoding, MacRomanEncoding, MacExpertEncoding (and PDFDocEncoding,
  // which no font may name but some files do); no value for any other name.
  [[nodiscard]] static std::optional<Base> by_name(std::string_view name) noexcept;

  // The glyph name of code in base; empty when undefined.
  [[nodiscard]] static std::string_view name(Base base, std::uint8_t code) noexcept {
    return table(base)[code];
  }
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_ENCODINGS_HPP
