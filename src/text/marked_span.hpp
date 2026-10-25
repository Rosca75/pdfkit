// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// MarkedSpan is one marked-content sequence (BDC ... EMC) whose properties
// carry text an extractor copies instead of the glyphs: /ActualText, /Alt or
// /E (ISO 32000-1 section 14.9, read; docs/DESIGN.md section 6.1). The
// interpreter lists every such span of a page in the order its BDC was met,
// and each glyph inside an /ActualText span names the outermost one, because
// an outer replacement text covers everything inside it. The strings are
// read as text strings (TextString) into UTF-8.

#ifndef PDFKIT_TEXT_MARKED_SPAN_HPP
#define PDFKIT_TEXT_MARKED_SPAN_HPP

#include <cstddef>
#include <optional>
#include <string>

namespace pdfkit::text {

class MarkedSpan {
 public:
  // The marked-content tag (Span, P, ...).
  std::string tag;
  std::optional<std::string> actual_text;
  std::optional<std::string> alt;
  std::optional<std::string> expansion;
  // The content stream walk and the BDC operator that opened the span.
  std::size_t stream = 0;
  std::size_t operation = 0;
  // The enclosing span with /ActualText, if any.
  std::optional<std::size_t> parent;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_MARKED_SPAN_HPP
