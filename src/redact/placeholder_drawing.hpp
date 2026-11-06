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
// every other run draws it filled (render mode 0). A placeholder followed by
// a bridge (PlaceholderFitting) ends with one space of its face shown under
// the bridge's character spacing, which paints nothing and carries the word
// break that stood before the next kept word.
// box() writes the operators of a box match instead: a filled rectangle
// inside a saved graphics state (q, the box's graphics state, the fill
// colour, cm with the anchor glyph's text matrix, re, f, Q), placed before
// the BT that opens the text object holding the line's first removed glyph,
// so it is painted under the same transformation and under the clip the
// glyphs were painted with, before a clipping render mode (4 to 7) adds the
// kept glyphs' outlines to the clip at ET. The graphics state, named
// PdfkitBoxGS in the resources the stream reads, sets the alpha of filling
// and stroking to 1, the blend mode to Normal and the soft mask to None
// (ISO 32000-1 section 8.4.5, read), so the box is opaque whatever state
// the text was drawn under; nothing else the box inherits changes what a
// rectangle fill paints.

#ifndef PDFKIT_REDACT_PLACEHOLDER_DRAWING_HPP
#define PDFKIT_REDACT_PLACEHOLDER_DRAWING_HPP

#include "redact/match_finder.hpp"
#include "redact/placeholder_fitting.hpp"
#include "syntax/dictionary.hpp"
#include "text/glyph_record.hpp"
#include "text/matrix.hpp"
#include "text/rect.hpp"
#include "writer/placeholder_font.hpp"

#include <string>
#include <string_view>

namespace pdfkit::redact {

class PlaceholderDrawing {
 public:
  // The operators drawing fit's placeholder at its anchor, in its face and
  // at its scale times the anchor's horizontal scaling, followed by its
  // bridge when it has one; invisible draws in render mode 3.
  [[nodiscard]] static std::string operators(const PlaceholderFitting::Result& fit,
                                             std::string_view placeholder, bool invisible);

  // The operator setting colour as the fill colour, empty for a colour
  // outside the device spaces.
  [[nodiscard]] static std::string fill(const text::Colour& colour);

  // The resource name of the box's graphics state.
  static constexpr std::string_view box_state = "PdfkitBoxGS";
  // The graphics state dictionary a box is painted under.
  [[nodiscard]] static syntax::Dictionary box_state_dictionary();

  // The operators painting the rectangle area, given in the text space of
  // text_matrix (under the CTM in force where they are written), filled
  // with colour.
  [[nodiscard]] static std::string box(const text::Matrix& text_matrix, const text::Rect& area,
                                       const MatchFinder::Colour& colour);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_PLACEHOLDER_DRAWING_HPP
