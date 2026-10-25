// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// HiddenText flags each glyph with the hidden-text classes of CLAUDE.md
// section 4 and docs/DESIGN.md chapter 8; every hidden glyph stays a glyph, is
// extracted and is redactable, because an extractor's copy reaches it:
//
// - render_invisible: render mode 3 or 7;
// - white_fill: the colour the mode paints with (the fill for 0, 2, 4 and 6,
//   the stroke for 1 and 5, both for 2 and 6) reads white, and no filled
//   figure that is not white and no image drawn earlier meets the glyph's
//   box;
// - clipped: the box lies wholly outside the clip bound in force;
// - off_media: the box lies wholly outside the media box;
// - off_crop: the box is not off the media box and lies wholly outside the
//   crop box;
// - optional_content_off: the default configuration hides a group the glyph
//   is drawn in;
// - duplicate: the glyph repeats an earlier glyph of the same code and font
//   (by name) whose origin lies within a tenth of the font size (a fake bold or a
//   shadow), docs/DESIGN.md chapter 7; it is linked to that earlier glyph,
//   which keeps it in its duplicates, so a redaction edits both operators;
// - covered: an image without a mask, or an opaque filled rectangle, drawn
//   later wholly contains the glyph's box.
//
// A glyph with an empty box (a zero size) is never clipped, off the page or
// covered, because nothing places it anywhere. Boxes are compared with a
// tolerance of 0.01 pt (CLAUDE.md section 9).

#ifndef PDFKIT_TEXT_HIDDEN_TEXT_HPP
#define PDFKIT_TEXT_HIDDEN_TEXT_HPP

#include "text/page_content.hpp"

namespace pdfkit::text {

class HiddenText {
 public:
  // Sets every glyph's hidden mask from content's figures, images and boxes.
  static void classify(PageContent& content);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_HIDDEN_TEXT_HPP
