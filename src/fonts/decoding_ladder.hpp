// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// DecodingLadder is the ladder of docs/DESIGN.md section 5.4, one function
// per font kind, each trying its rungs in this order and no other:
// /ToUnicode; the glyph name through the encoding and the glyph list; the
// font program's own map (a TrueType cmap, a Type 1 or CFF built-in encoding,
// a CFF charset); unreadable last. A /ToUnicode destination that is empty, a
// control character other than tab or a private-use code point falls through,
// because it is a producer's placeholder, not an answer. A Type 3 font is
// reached through the first two rungs only. A composite font whose encoding
// is a predefined CMap other than the identities and the Unicode CMaps has no
// glyph-name rung without CMap data, so its codes without /ToUnicode read as
// unreadable. The answer's text is decomposed through the ligature table.

#ifndef PDFKIT_FONTS_DECODING_LADDER_HPP
#define PDFKIT_FONTS_DECODING_LADDER_HPP

#include "fonts/decoding_rung.hpp"
#include "fonts/pdf_font.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pdfkit::fonts {

class Cmap;
class FontProgram;
class SimpleFont;
class Type0Font;

class DecodingLadder {
 public:
  [[nodiscard]] static PdfFont::Answer simple(const SimpleFont& font, std::uint32_t code);
  [[nodiscard]] static PdfFont::Answer composite(const Type0Font& font, std::uint32_t code);

  // The text a /ToUnicode map gives code when it is usable as an answer.
  [[nodiscard]] static std::optional<std::string> from_to_unicode(const Cmap* map,
                                                                  std::uint32_t code);
  // UTF-8 text when every code point of it is usable (GlyphList::usable) and
  // it is not empty.
  [[nodiscard]] static bool usable_text(std::string_view text) noexcept;
  // The answer for text found by rung, decomposed through the ligature table.
  [[nodiscard]] static PdfFont::Answer answer(std::string text, DecodingRung rung);
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_DECODING_LADDER_HPP
