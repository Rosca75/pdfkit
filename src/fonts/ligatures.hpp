// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Ligatures decomposes the Latin ligature characters U+FB00 to U+FB06 (ff,
// fi, fl, ffi, ffl, long s t, st) into the letters they draw, docs/DESIGN.md
// section 5.5, so a search for "office" finds a word drawn with an "ffi"
// glyph. The table is the Unicode compatibility decompositions of those seven
// characters (UnicodeData.txt, recalled). The glyph names ff, fi, fl, ffi, ffl
// and st reach the same characters through the glyph list first. Combining
// sequences are kept as written; folding belongs to the matcher.

#ifndef PDFKIT_FONTS_LIGATURES_HPP
#define PDFKIT_FONTS_LIGATURES_HPP

#include <optional>
#include <string>
#include <string_view>

namespace pdfkit::fonts {

class Ligatures {
 public:
  // The letters of one ligature code point, or empty for any other.
  [[nodiscard]] static std::string_view letters(char32_t code_point) noexcept;

  // The UTF-8 text with every ligature replaced by its letters, or no value
  // when the text holds none.
  [[nodiscard]] static std::optional<std::string> decompose(std::string_view text);
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_LIGATURES_HPP
