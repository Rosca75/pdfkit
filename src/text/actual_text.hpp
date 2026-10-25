// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ActualText applies the fourth rung of the decoding ladder, docs/DESIGN.md
// section 5.4: a marked-content span's /ActualText replaces the decoded text
// of the span's glyphs when the two differ, because LibreOffice and Chrome
// write it exactly where /ToUnicode cannot express a ligature or a cluster
// (the pdf.js report docs/SOURCES.md lists, read in the design record). Every glyph of a replaced
// span records the rung actual_text; the replacement text goes to the span's
// first glyph in draw order and the others are marked as covered by the span
// with no text of their own, so the text is read once and a match over it
// still maps to every glyph that drew it. A span whose glyphs already read as
// its /ActualText is left as decoded. The outermost span with /ActualText is
// the one applied, as the interpreter records it.

#ifndef PDFKIT_TEXT_ACTUAL_TEXT_HPP
#define PDFKIT_TEXT_ACTUAL_TEXT_HPP

#include "text/glyph_record.hpp"
#include "text/marked_span.hpp"

#include <vector>

namespace pdfkit::text {

class ActualText {
 public:
  // Applies spans to glyphs (a page's records in draw order).
  static void apply(std::vector<GlyphRecord>& glyphs, const std::vector<MarkedSpan>& spans);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_ACTUAL_TEXT_HPP
