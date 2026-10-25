// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// XyCut orders a page's text blocks for reading by recursive XY-cut, docs/
// DESIGN.md chapter 7. Ported from PdfPig (Apache-2.0),
// src/UglyToad.PdfPig.DocumentLayoutAnalysis/PageSegmenter/RecursiveXYCut.cs
// at commit e17fc2d577a24db986d8642ec7190188d0f6a0a9 (the algorithm read
// there, the code written here): at each node the boxes are projected on
// the x axis and cut where the projection has a gap, columns first, left to
// right; a node that has no vertical gap is projected on the y axis and cut
// where that projection has a gap, top to bottom; each part is cut again
// until no gap remains. The port's own changes: it orders whole blocks
// rather than segmenting words (the blocks come from Docstrum), a gap is any
// positive distance between projections rather than the dominant font width
// (blocks already absorb word and line spacing), a node with no gap on either
// axis is ordered top to bottom then left to right, every sort is stable
// and every key is rounded to 0.01 pt (CLAUDE.md section 9), and the
// boxes are given in the page's reading frame (the dominant text direction
// turned to the x axis) so a rotated page is cut as it reads. A
// right-to-left page orders its columns right to left.

#ifndef PDFKIT_TEXT_XY_CUT_HPP
#define PDFKIT_TEXT_XY_CUT_HPP

#include "text/rect.hpp"

#include <cstddef>
#include <vector>

namespace pdfkit::text {

class XyCut {
 public:
  // The indices of boxes in reading order.
  [[nodiscard]] static std::vector<std::size_t> order(const std::vector<Rect>& boxes,
                                                      bool right_to_left = false);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_XY_CUT_HPP
