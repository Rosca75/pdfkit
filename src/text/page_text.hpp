// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PageText is the per-page result of text extraction, docs/DESIGN.md
// chapters 6 to 8: the interpreted content with its glyph records, the
// reading order over them (the visible set first, then the hidden set), the
// visible blocks and the hidden blocks with their lines and words, the
// counts the reports need (codes per decoding rung, unreadable codes per font
// name, hidden glyphs per class) and the page's warnings of docs/DESIGN.md
// chapter 19: unreadable_text, hidden_text, font_not_embedded,
// damaged_stream and resource_missing.

#ifndef PDFKIT_TEXT_PAGE_TEXT_HPP
#define PDFKIT_TEXT_PAGE_TEXT_HPP

#include "text/hidden_class.hpp"
#include "text/page_content.hpp"
#include "text/text_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pdfkit::text {

class PageText {
 public:
  struct Warning {
    std::string code;
    std::string message;
  };

  std::uint32_t page = 0;
  PageContent content;
  // Draw indices in reading order: the visible glyphs, then the hidden ones.
  std::vector<std::size_t> order;
  std::size_t visible = 0;
  std::vector<TextLayout::Block> blocks;
  std::vector<TextLayout::Block> hidden_blocks;
  // Whether the tagged structure ordered the visible blocks.
  bool structure_order = false;

  // Codes per rung, in the order of fonts::DecodingRung.
  std::array<std::uint64_t, 5> rungs{};
  std::map<std::string, std::uint64_t> unreadable;
  // Hidden glyphs per class, in the order of hidden_classes, and in all.
  std::array<std::uint64_t, hidden_classes.size()> hidden_counts{};
  std::uint64_t hidden = 0;
  std::vector<Warning> warnings;

  [[nodiscard]] std::uint64_t unreadable_count() const;
  // The page's text in reading order: words joined by a space, lines by a
  // line feed, blocks by an empty line; the hidden blocks follow the visible
  // ones when hidden_too. A duplicate copy of a word is read once.
  [[nodiscard]] std::string text(bool hidden_too = true) const;
  // The text of a list of blocks in the same form.
  [[nodiscard]] static std::string text_of(const std::vector<TextLayout::Block>& blocks);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_PAGE_TEXT_HPP
