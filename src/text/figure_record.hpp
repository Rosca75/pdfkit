// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FigureRecord is one group of path-painting operators between a q and its Q
// (or between the stream's start or end and the nearest q or Q), with its
// device box and operator range, docs/DESIGN.md section 6.1. Text drawn as
// vector outlines is a figure, not text (section 5.4). Each painting
// operator inside the group is kept as a paint with its own box, whether it
// fills, whether its fill is white and opaque, and whether its path is one
// axis-aligned rectangle, because the hidden-text classes white_fill and
// covered (chapter 8) ask what was painted under or over a glyph, and only a
// rectangle's bound is the area it paints.

#ifndef PDFKIT_TEXT_FIGURE_RECORD_HPP
#define PDFKIT_TEXT_FIGURE_RECORD_HPP

#include "text/rect.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pdfkit::text {

class FigureRecord {
 public:
  // One painting operator (S, s, f, F, f*, B, B*, b, b*, sh).
  struct Paint {
    std::size_t operation = 0;
    // The page's event order shared with glyphs and images.
    std::size_t sequence = 0;
    // The bound of the painted path in device space, cut by the clip.
    Rect box;
    bool filled = false;
    bool stroked = false;
    // The fill colour reads white.
    bool white = false;
    // The fill is opaque: alpha 1, no soft mask, the normal blend mode.
    bool opaque = true;
    // The path is one rectangle under an axis-aligned CTM, so box is exactly
    // the area painted.
    bool rectangular = false;
  };

  std::uint32_t page = 0;
  // The content stream walk (an index into PageContent::streams).
  std::size_t stream = 0;
  // [first_operation, last_operation] of the painting operators.
  std::size_t first_operation = 0;
  std::size_t last_operation = 0;
  Rect box;
  std::vector<Paint> paints;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_FIGURE_RECORD_HPP
