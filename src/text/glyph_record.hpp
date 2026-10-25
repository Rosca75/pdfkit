// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// GlyphRecord is one shown glyph, CLAUDE.md section 4 and docs/DESIGN.md
// section 6.2: where its code sits (the page, the content stream walk, the
// operator, the operand and the byte range of the code's encoded form inside
// that stream's bytes), what it is (font, code, decoded text and the rung
// that answered it), where it went (the text state and matrices it was drawn
// under, its advance, its device-space origin, advance end and box), how it
// was drawn (render mode, fill colour, marked content, optional content,
// clip) and which hidden classes it belongs to. The byte range is what a
// redaction edits and the re-serialisation identity proves, so it names the
// bytes as written: two hexadecimal digits for each byte of a <hex> string,
// an escape such as \( or \101 for a literal one. Every field is a value; the
// font is shared so a later edit can ask it for widths.

#ifndef PDFKIT_TEXT_GLYPH_RECORD_HPP
#define PDFKIT_TEXT_GLYPH_RECORD_HPP

#include "fonts/decoding_rung.hpp"
#include "fonts/pdf_font.hpp"
#include "text/colour.hpp"
#include "text/hidden_class.hpp"
#include "text/matrix.hpp"
#include "text/rect.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::text {

class GlyphRecord {
 public:
  // The page, 1-based.
  std::uint32_t page = 0;
  // The content stream walk it was drawn in: an index into the page's
  // PageContent::streams (0 is the page's own /Contents).
  std::size_t stream = 0;
  // The operator's index among the stream's operations.
  std::size_t operation = 0;
  // The operand holding the code: for TJ the index of the string inside the
  // array, for Tj and ' zero, for " two (its third operand).
  std::size_t operand = 0;
  // [code_start, code_end) of the code's encoded bytes in the stream's bytes.
  std::size_t code_start = 0;
  std::size_t code_end = 0;
  // The string operand was written <hex> rather than (literal).
  bool hex_string = false;
  // The glyph's place among the page's glyphs in draw order, from 0.
  std::size_t draw = 0;
  // The glyph's place among everything the page drew (glyphs, paints,
  // images), from 0, so later and earlier can be compared across kinds.
  std::size_t sequence = 0;

  std::shared_ptr<fonts::PdfFont> font;
  // The font dictionary's object number, 0 for a direct dictionary.
  std::uint32_t font_object = 0;
  // The code and the bytes it takes.
  std::uint32_t code = 0;
  std::size_t code_length = 1;
  // UTF-8; empty when unreadable, and for every glyph of an /ActualText span
  // but its first.
  std::string text;
  fonts::DecodingRung rung = fonts::DecodingRung::unreadable;
  bool decomposed = false;

  // w0 (or w1 for vertical writing) in thousandths of text space.
  double width = 0.0;
  // The preceding TJ number, zero when none.
  double adjustment = 0.0;
  // The displacement the glyph applied to the text matrix in text space
  // (tx, or ty for vertical writing), Tc and Tw included.
  double advance = 0.0;
  bool vertical = false;

  // The text state at the glyph: the text matrix before it was shown, the
  // CTM, Tc, Tw, Tz, TL, Ts, Tfs and Tr.
  Matrix text_matrix;
  Matrix ctm;
  double char_spacing = 0.0;
  double word_spacing = 0.0;
  double horizontal_scaling = 100.0;
  double leading = 0.0;
  double rise = 0.0;
  double font_size = 0.0;
  int render_mode = 0;

  // Device space: the origin (rise included), the end of the advance, the
  // box (origin to the glyph width, descent to ascent), the font size the
  // reader sees, and the direction of the advance in degrees from the x axis.
  Matrix::Point origin;
  Matrix::Point advance_end;
  Rect box;
  double size = 0.0;
  double angle = 0.0;

  Colour fill;
  // The stroke colour reads white (what render modes 1, 2, 5 and 6 paint
  // with), for the white_fill class.
  bool stroke_white = false;
  // The innermost marked-content tag and its /MCID, -1 for none.
  std::string tag;
  std::int64_t mcid = -1;
  // The /ActualText span covering the glyph (an index into the page's
  // spans), and whether its text went to an earlier glyph of the span.
  std::optional<std::size_t> actual_text;
  bool actual_text_covered = false;
  // The optional-content groups or membership dictionaries in force, by
  // object number, and whether the default configuration hides any of them.
  std::vector<std::uint32_t> optional_content;
  bool optional_content_off = false;
  Rect clip = Rect::everything();

  // The hidden classes, a mask of HiddenClass bits.
  std::uint16_t hidden = 0;
  // A duplicate's kept partner (its draw index), and the duplicates a kept
  // record carries, so a redaction edits both operators.
  std::optional<std::size_t> duplicate_of;
  std::vector<std::size_t> duplicates;

  [[nodiscard]] bool has(HiddenClass value) const noexcept {
    return (hidden & hidden_bit(value)) != 0;
  }
  // Whether the decoded text is white space only (a space glyph); an
  // unreadable glyph is not.
  [[nodiscard]] bool is_space() const noexcept;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_GLYPH_RECORD_HPP
