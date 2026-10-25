// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Docstrum groups a page's text lines into blocks, docs/DESIGN.md chapter 7.
// Ported from PdfPig (Apache-2.0),
// src/UglyToad.PdfPig.DocumentLayoutAnalysis/PageSegmenter/
// DocstrumBoundingBoxes.cs at commit e17fc2d577a24db986d8642ec7190188d0f6a0a9
// (the algorithm read there, the code written here): the between-line
// distance is estimated as the mean of the most populated bin of a histogram
// (bins of 10 pt, narrowed to the largest distance when that is smaller) of
// each line's distance to its nearest line below; two lines join one block
// when they run in the same direction (PdfPig's angular bound of 30 degrees,
// here the same rounded direction), their extents along that direction
// overlap, and the perpendicular gap between the lower edge of one and the
// upper edge of the other is at most 1.3 times the estimate; blocks are the
// connected groups of that relation. The port's own changes: it groups the
// layout's lines (already built from glyphs by baseline) rather than words,
// lines that overlap across their direction (a superscript, a drop
// capital) join when their extents overlap, a page with no estimate uses half
// the median line height, every sort is stable on keys rounded to 0.01 pt,
// and the groups come back in the order of their first line, so the result
// is a function of the input alone (CLAUDE.md section 9); a ruled line drawn
// between two text lines keeps them apart, so a ruled table's rows are
// separate blocks.

#ifndef PDFKIT_TEXT_DOCSTRUM_HPP
#define PDFKIT_TEXT_DOCSTRUM_HPP

#include <cstddef>
#include <vector>

namespace pdfkit::text {

class Docstrum {
 public:
  // A line in its own frame: the direction bucket (degrees), its extent
  // along the direction [along_low, along_high], its extent across it
  // [across_low, across_high] (upward from the baseline side) and its size.
  struct Line {
    int direction = 0;
    double along_low = 0.0;
    double along_high = 0.0;
    double across_low = 0.0;
    double across_high = 0.0;
    double size = 0.0;
  };

  // A drawn rule in the frame of a direction: it runs along
  // [along_low, along_high] at the height across.
  struct Rule {
    int direction = 0;
    double along_low = 0.0;
    double along_high = 0.0;
    double across = 0.0;
  };

  // The blocks: groups of line indices, each ascending, ordered by their
  // first index. Two lines with a rule of their direction between them that
  // spans both (a table's row separator) are not joined.
  [[nodiscard]] static std::vector<std::vector<std::size_t>> blocks(
      const std::vector<Line>& lines, const std::vector<Rule>& rules = {});

  // The between-line distance the grouping used for lines.
  [[nodiscard]] static double between_line_distance(const std::vector<Line>& lines);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_DOCSTRUM_HPP
