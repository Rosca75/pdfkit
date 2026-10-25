// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// GlyphList turns a glyph name into Unicode text, docs/DESIGN.md section 5.2:
// first through the Adobe Glyph List and the ITC Zapf Dingbats list (the
// generated table, BSD-3-Clause, read), then through the naming conventions
// pdf.js reads (src/core/fonts.js and unicode.js, read): "uniXXXX" and its
// sequences of four-digit groups, "uXXXX" to "uXXXXXX", a suffix after a
// period dropped ("a.sc" reads "a"), and "a_b" read as the ligature of its
// parts. The names that count glyphs or codes rather than characters
// ("gNN", "glyphNN", "cidNN", "GNN", "Cdd", "Cxx") give no text here; their
// number is handed to the font program, which alone knows what it draws.

#ifndef PDFKIT_FONTS_GLYPH_LIST_HPP
#define PDFKIT_FONTS_GLYPH_LIST_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pdfkit::fonts {

class GlyphList {
 public:
  // What a number-bearing glyph name counts.
  struct Number {
    enum class Kind : std::uint8_t {
      // gNN, glyphNN, GNN: a glyph index of the font program.
      glyph,
      // cidNN: a CID.
      cid,
      // Cdd, Cxx: a character code.
      code,
    };
    Kind kind = Kind::glyph;
    std::uint32_t value = 0;
  };

  // The UTF-8 text of a glyph name, or no value when neither the lists nor the
  // conventions answer. The text is never empty, never a control character,
  // a surrogate or a private-use code point.
  [[nodiscard]] static std::optional<std::string> to_unicode(std::string_view name);

  // The text of a name in the Adobe Glyph List or the Zapf Dingbats list
  // alone, without the conventions.
  [[nodiscard]] static std::optional<std::string> lookup(std::string_view name);

  // The number a "gNN", "glyphNN", "cidNN", "GNN", "Cdd" or "Cxx" name
  // carries; no value for any other name. A decimal reading is tried before a
  // hexadecimal one.
  [[nodiscard]] static std::optional<Number> number(std::string_view name) noexcept;

  // Whether a code point may stand as decoded text: not a control character
  // other than tab, not a surrogate, not a private-use code point, not a
  // noncharacter and not past U+10FFFF.
  [[nodiscard]] static bool usable(char32_t code_point) noexcept;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_GLYPH_LIST_HPP
