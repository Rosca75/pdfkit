// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PageContent is what the content interpreter reads from one page, docs/
// DESIGN.md chapter 6: every content stream walk (the page's /Contents and
// each form draw) with its bytes and operations, every shown glyph as a
// GlyphRecord in draw order, every figure, every image occurrence in draw
// order through forms, the marked-content spans carrying replacement text,
// the unreadable codes per font name, the fonts whose widths are estimated,
// and the faults met (a damaged stream, a refused self-drawing form, a form
// past the depth limit, a q past the stack bound). It is the one reading of a
// page that extraction, the image listing and redaction all start from.

#ifndef PDFKIT_TEXT_PAGE_CONTENT_HPP
#define PDFKIT_TEXT_PAGE_CONTENT_HPP

#include "syntax/page.hpp"
#include "text/content_stream.hpp"
#include "text/figure_record.hpp"
#include "text/glyph_record.hpp"
#include "text/image_occurrence.hpp"
#include "text/marked_span.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pdfkit::text {

class PageContent {
 public:
  // 1-based.
  std::uint32_t page = 0;
  syntax::Page::Box media_box;
  syntax::Page::Box crop_box;
  int rotate = 0;

  std::vector<ContentStream> streams;
  // In draw order: glyphs[i].draw == i.
  std::vector<GlyphRecord> glyphs;
  std::vector<FigureRecord> figures;
  // In draw order: images[i].ordinal == i.
  std::vector<ImageOccurrence> images;
  std::vector<MarkedSpan> spans;

  // Unreadable codes per font name, and the names of fonts drawn with
  // estimated widths.
  std::map<std::string, std::uint64_t> unreadable;
  std::set<std::string> estimated_widths;
  // A walk read only up to a fault (a damaged stream or a lexer recovery).
  bool damaged = false;
  // What went wrong, each once, in the order met.
  std::vector<std::string> faults;

  // The fault, recorded once.
  void fault(std::string message);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_PAGE_CONTENT_HPP
