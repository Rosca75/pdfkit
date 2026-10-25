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
  // Content a draw names that the interpreter does not walk (docs/DESIGN.md
  // section 6.1): a tiling pattern set as a colour, or the group of a soft
  // mask set by an ExtGState. An image inside one is listed with the warning
  // image_in_pattern and never numbered among the page's occurrences.
  struct Unwalked {
    enum class Kind : std::uint8_t { pattern, soft_mask };
    Kind kind = Kind::pattern;
    // The resolved pattern stream or soft-mask group form, and the reference
    // that named it (number 0 when written directly).
    syntax::Object content;
    syntax::Object::Reference reference;
    // The resources its content draws with: its own /Resources, or null.
    syntax::Object resources;
    // The walk and the operator (scn, SCN or gs) that set it.
    std::size_t stream = 0;
    std::size_t operation = 0;
    std::vector<std::uint32_t> form_path;
  };

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
  // Patterns and soft masks set on the page, in the order set.
  std::vector<Unwalked> unwalked;

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
