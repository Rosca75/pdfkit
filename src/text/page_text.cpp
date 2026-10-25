// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The text form of a page's blocks. A line with no word (only spaces or
// nothing readable) writes nothing, so it adds no empty line. A word whose
// only hidden class is duplicate is the second copy of a word already read (a
// fake bold, a shadow) and is not read again; its glyphs stay in the hidden
// blocks and the glyph list.

#include "text/page_text.hpp"

#include <numeric>

namespace pdfkit::text {

std::uint64_t PageText::unreadable_count() const {
  return std::accumulate(unreadable.begin(), unreadable.end(), std::uint64_t{0},
                         [](std::uint64_t sum, const auto& entry) { return sum + entry.second; });
}

std::string PageText::text_of(const std::vector<TextLayout::Block>& blocks) {
  const auto duplicate_only = hidden_bit(HiddenClass::duplicate);
  std::string out;
  for (const TextLayout::Block& block : blocks) {
    std::string block_text;
    for (const TextLayout::Line& line : block.lines) {
      std::string line_text;
      for (const TextLayout::Word& word : line.words) {
        if (word.text.empty() || word.hidden == duplicate_only) {
          continue;
        }
        if (!line_text.empty()) {
          line_text += ' ';
        }
        line_text += word.text;
      }
      if (line_text.empty()) {
        continue;
      }
      if (!block_text.empty()) {
        block_text += '\n';
      }
      block_text += line_text;
    }
    if (block_text.empty()) {
      continue;
    }
    if (!out.empty()) {
      out += "\n\n";
    }
    out += block_text;
  }
  return out;
}

std::string PageText::text(bool hidden_too) const {
  std::string out = text_of(blocks);
  if (hidden_too) {
    const std::string hidden_text = text_of(hidden_blocks);
    if (!hidden_text.empty()) {
      if (!out.empty()) {
        out += "\n\n";
      }
      out += hidden_text;
    }
  }
  return out;
}

}  // namespace pdfkit::text
