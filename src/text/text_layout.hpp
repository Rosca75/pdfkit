// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TextLayout turns a page's glyph records into words, lines and blocks in
// reading order, docs/DESIGN.md chapter 7:
//
// - lines: glyphs whose direction (rounded to a degree) agrees and whose
//   baselines (the text-line position, the rise left out) agree within a
//   fifth of the font size, in the baseline's own direction, so rotated text
//   forms lines along its rotation and vertical text forms columns read top to
//   bottom; a gap along the line wider than 2.0 times the font size ends the
//   line there, because it is a block boundary (two columns sharing a
//   baseline);
// - words: consecutive glyphs of a line are one word while the gap between
//   the first's advance end and the second's origin is under 0.3 times the
//   font size; a space glyph ends a word and belongs to none;
// - direction: a line whose strong characters are mostly right-to-left
//   (Hebrew, Arabic) reads right to left, in the order the content drew it
//   when the producer drew it in logical order, else reversed from visual
//   order (the author decision of docs/DESIGN.md chapter 7);
// - blocks: lines grouped by Docstrum, ordered by recursive XY-cut in the
//   page's reading frame, or by the tagged structure's order when the caller
//   supplies one; lines inside a block from the top down;
// - hyphenation: a line whose last word ends in a hyphen (U+002D or U+00AD)
//   before a line starting with a lower-case letter marks that word as
//   joined to the next, the hyphen staying on the first half, so a match
//   finds the word across the break; a capital after the hyphen keeps the two
//   apart, because "Jean-" / "Pierre" is a name.
//
// The result is a reading order over the glyphs given (a permutation of draw
// indices) and, for each word, line and block, the half-open range of
// positions in that order it covers. Every sort is stable and every
// geometric key is rounded to 0.01 pt (CLAUDE.md section 9), so the same
// glyphs in any draw order give the same output.

#ifndef PDFKIT_TEXT_TEXT_LAYOUT_HPP
#define PDFKIT_TEXT_TEXT_LAYOUT_HPP

#include "text/glyph_record.hpp"
#include "text/rect.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pdfkit::text {

class TextLayout {
 public:
  struct Word {
    std::string text;
    Rect box;
    // [first, last) positions in the reading order.
    std::size_t first = 0;
    std::size_t last = 0;
    // The word ends in a hyphen that joins it to the first word of the next
    // line.
    bool hyphenated = false;
    // The hidden classes of its glyphs, a mask of HiddenClass bits.
    std::uint16_t hidden = 0;
  };

  struct Line {
    Rect box;
    std::size_t first = 0;
    std::size_t last = 0;
    // The direction of reading in degrees from the x axis (0 left to right,
    // -90 top to bottom), and "ltr", "rtl" or "ttb".
    double angle = 0.0;
    std::string direction = "ltr";
    std::vector<Word> words;
  };

  struct Block {
    Rect box;
    std::size_t first = 0;
    std::size_t last = 0;
    std::vector<Line> lines;
  };

  struct Result {
    // Draw indices in reading order.
    std::vector<std::size_t> order;
    std::vector<Block> blocks;
  };

  // Lays out glyphs[i] for each i of members. offset is added to every range
  // (the hidden set's ranges continue after the visible set's). rank, when
  // not empty, maps a marked-content id to its place in the structure order,
  // which then orders the blocks.
  [[nodiscard]] static Result run(const std::vector<GlyphRecord>& glyphs,
                                  const std::vector<std::size_t>& members, std::size_t offset,
                                  const std::map<std::int64_t, std::size_t>& rank = {});

  // Marks the last word of line before as hyphenated when its hyphen joins
  // the first word of line after (the rule above).
  static void join_hyphen(Line& before, const Line& after);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_TEXT_LAYOUT_HPP
