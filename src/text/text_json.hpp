// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TextJson writes one page of the pdfkit_text result in the shape the pdfkit_text
// section of docs/ABI.md fixes: the page's text, its blocks and hidden blocks (each with
// lines and words, every one with its box and the half-open range of glyph
// positions it covers), the unreadable codes per font, the hidden glyphs per
// class, the codes per decoding rung, the warnings, and on request the glyph
// list with every field of the glyph record and the re-serialisation check of
// every content stream walk. Keys are written in a fixed order and every
// geometric number is rounded to 0.01 pt, so one page always writes the same
// bytes (CLAUDE.md section 9).

#ifndef PDFKIT_TEXT_TEXT_JSON_HPP
#define PDFKIT_TEXT_TEXT_JSON_HPP

#include "platform/json_writer.hpp"
#include "text/page_text.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pdfkit::text {

class TextJson {
 public:
  struct Options {
    // Write the blocks, lines and words (the page's text is always written).
    bool blocks = true;
    // Write the glyph list and the marked-content spans.
    bool glyphs = false;
    // Write the re-serialisation check of every walk.
    bool reserialisation = false;
  };

  // What writing a page's walks back gave: walks written, walks identical to
  // their bytes, walks read only up to a fault, and the mismatches named.
  struct Reserialisation {
    std::size_t streams = 0;
    std::size_t identical = 0;
    std::size_t damaged = 0;
    // Walks that interpret without a fault and still differ.
    std::size_t failed = 0;
    std::vector<std::string> mismatches;
  };

  [[nodiscard]] static Reserialisation reserialise(const PageText& page);

  // Writes page as one JSON object into writer.
  static void write_page(platform::JsonWriter& writer, const PageText& page,
                         const Options& options);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_TEXT_JSON_HPP
