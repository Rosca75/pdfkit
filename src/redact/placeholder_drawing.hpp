// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PlaceholderDrawing writes the operators that draw one placeholder,
// docs/DESIGN.md sections 12 and 13: its own text object inside a saved
// graphics state (q, the fill colour, BT, Tf, Tm, Tz, Ts, Tc, Tw, Tr, the Tj
// of its two-byte codes, ET, Q), placed after the end of the text object
// that held the removed glyph, in the same content stream, so it is drawn
// under the same transformation and clip. It starts at the removed run's
// first glyph with that glyph's text matrix (its position and rotation),
// font size, rise and horizontal scaling (times the fitting's scale), and
// paints with the glyph's fill colour when that colour is in a device space;
// a colour in another space is the one in force where the placeholder is
// drawn, which is the glyph's unless the content changed it after the glyph.
// A run whose glyphs were all invisible (render mode 3 or 7) draws its
// placeholder in render mode 3, so an invisible layer stays invisible;
// every other run draws it filled (render mode 0).

#ifndef PDFKIT_REDACT_PLACEHOLDER_DRAWING_HPP
#define PDFKIT_REDACT_PLACEHOLDER_DRAWING_HPP

#include "text/glyph_record.hpp"
#include "writer/placeholder_font.hpp"

#include <string>
#include <string_view>

namespace pdfkit::redact {

class PlaceholderDrawing {
 public:
  // The operators drawing placeholder at anchor in face, its horizontal
  // scaling scale times the anchor's; invisible draws in render mode 3.
  [[nodiscard]] static std::string operators(const text::GlyphRecord& anchor,
                                             std::string_view placeholder,
                                             writer::PlaceholderFont::Face face, double scale,
                                             bool invisible);

  // The operator setting colour as the fill colour, empty for a colour
  // outside the device spaces.
  [[nodiscard]] static std::string fill(const text::Colour& colour);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_PLACEHOLDER_DRAWING_HPP
