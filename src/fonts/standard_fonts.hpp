// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// StandardFonts answers for the fourteen standard fonts of ISO 32000-1
// section 9.6.2.2 (read) when a file embeds no program, docs/DESIGN.md
// section 5.6: which Core 14 face a /BaseFont names, through the subset prefix
// ("ABCDEF+") stripped and the aliases viewers apply (Arial and ArialMT read
// as Helvetica, Times New Roman as Times, Courier New as Courier, in their
// ",Bold", ",Italic", ",BoldItalic", "-Bold", "-Italic", "-BoldMT" and like
// forms, as pdf.js src/core/standard_fonts.js getStdFontMap lists them, read),
// and each face's widths, built-in encoding and vertical metrics from the
// generated Core 14 table.

#ifndef PDFKIT_FONTS_STANDARD_FONTS_HPP
#define PDFKIT_FONTS_STANDARD_FONTS_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pdfkit::fonts {

class StandardFonts {
 public:
  // The faces in the order of ISO 32000-1 section 9.6.2.2.
  enum class Face : std::uint8_t {
    times_roman,
    times_bold,
    times_italic,
    times_bold_italic,
    helvetica,
    helvetica_bold,
    helvetica_oblique,
    helvetica_bold_oblique,
    courier,
    courier_bold,
    courier_oblique,
    courier_bold_oblique,
    symbol,
    zapf_dingbats,
  };

  // A /BaseFont without its subset prefix: "ABCDEF+Name" reads "Name".
  [[nodiscard]] static std::string_view strip_subset(std::string_view name) noexcept;

  // The Core 14 face a /BaseFont names, directly or through an alias; no
  // value for any other font.
  [[nodiscard]] static std::optional<Face> find(std::string_view base_font) noexcept;

  [[nodiscard]] static std::string_view name(Face face) noexcept;
  // The width of a glyph in thousandths of an em, or no value when the face
  // has no glyph of that name.
  [[nodiscard]] static std::optional<int> width(Face face, std::string_view glyph) noexcept;
  // The glyph the face's built-in encoding gives code (StandardEncoding for
  // the text faces, the AFM's own codes for Symbol and ZapfDingbats).
  [[nodiscard]] static std::string_view encoded_name(Face face, std::uint8_t code) noexcept;
  [[nodiscard]] static bool is_symbolic(Face face) noexcept {
    return face == Face::symbol || face == Face::zapf_dingbats;
  }
  [[nodiscard]] static bool is_bold(Face face) noexcept;
  [[nodiscard]] static int ascender(Face face) noexcept;
  [[nodiscard]] static int descender(Face face) noexcept;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_STANDARD_FONTS_HPP
