// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The replacement pass. Spans are visited in the order their BDC was met and
// glyphs in draw order, so the result depends on the content alone.

#include "text/actual_text.hpp"

#include <cstddef>
#include <map>
#include <string>

namespace pdfkit::text {

void ActualText::apply(std::vector<GlyphRecord>& glyphs, const std::vector<MarkedSpan>& spans) {
  std::map<std::size_t, std::vector<std::size_t>> members;
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    if (glyphs[i].actual_text && *glyphs[i].actual_text < spans.size()) {
      members[*glyphs[i].actual_text].push_back(i);
    }
  }
  for (const auto& [span, indices] : members) {
    const std::optional<std::string>& replacement = spans[span].actual_text;
    if (!replacement || indices.empty()) {
      continue;
    }
    std::string decoded;
    for (const std::size_t index : indices) {
      decoded += glyphs[index].text;
    }
    if (decoded == *replacement) {
      continue;
    }
    bool first = true;
    for (const std::size_t index : indices) {
      GlyphRecord& glyph = glyphs[index];
      glyph.rung = fonts::DecodingRung::actual_text;
      glyph.decomposed = false;
      if (first) {
        glyph.text = *replacement;
        first = false;
      } else {
        glyph.text.clear();
        glyph.actual_text_covered = true;
      }
    }
  }
}

}  // namespace pdfkit::text
