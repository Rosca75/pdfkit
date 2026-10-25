// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// DecodingRung names which step of the decoding ladder answered a code
// (CLAUDE.md section 4, docs/DESIGN.md section 5.4): the font's /ToUnicode
// map, the glyph name through the encoding and the glyph list, the font
// program's own map, a marked-content /ActualText span (set by the content
// interpreter, which alone sees marked content), or no rung at all. Every
// decoded code carries its rung, so a disagreement with another reader can be
// traced to the step that produced it.

#ifndef PDFKIT_FONTS_DECODING_RUNG_HPP
#define PDFKIT_FONTS_DECODING_RUNG_HPP

#include <cstdint>
#include <string_view>

namespace pdfkit::fonts {

enum class DecodingRung : std::uint8_t {
  to_unicode,
  glyph_name,
  font_program,
  actual_text,
  unreadable,
};

// The stable name of a rung, as reports and the glyph record write it.
[[nodiscard]] constexpr std::string_view decoding_rung_name(DecodingRung rung) noexcept {
  switch (rung) {
    case DecodingRung::to_unicode:
      return "to_unicode";
    case DecodingRung::glyph_name:
      return "glyph_name";
    case DecodingRung::font_program:
      return "font_program";
    case DecodingRung::actual_text:
      return "actual_text";
    case DecodingRung::unreadable:
      return "unreadable";
  }
  return "unreadable";
}

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_DECODING_RUNG_HPP
