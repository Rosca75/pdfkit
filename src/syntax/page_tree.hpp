// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PageTree walks the page tree of ISO 32000-1 section 7.7.3 (read) depth first
// from the catalog's /Pages, docs/DESIGN.md chapter 4: a /Kids entry already
// on the current path is a cycle and is skipped with a repair note; a kid that
// is neither /Pages nor /Page is a node when it has /Kids and a page
// otherwise, as pdf.js reads it (read in the design record); /Resources,
// /MediaBox, /CropBox and /Rotate are inherited. A missing /MediaBox is US
// Letter, as viewers default it. The walk is iterative and bounded by the page
// ceiling and a visit count, so neither a deep nor a self-multiplying tree
// exhausts the stack or the clock.

#ifndef PDFKIT_SYNTAX_PAGE_TREE_HPP
#define PDFKIT_SYNTAX_PAGE_TREE_HPP

#include "syntax/object.hpp"
#include "syntax/page.hpp"

#include <optional>
#include <vector>

namespace pdfkit::syntax {

class Document;

class PageTree {
 public:
  // The pages under the catalog's /Pages, in tree order. Past the document's
  // page ceiling, throws the page_ceiling refusal.
  [[nodiscard]] static std::vector<Page> walk(Document& document);

  // Pages built from page objects found by reconstruction, in the order given,
  // with only their own attributes; for a file whose tree yields nothing.
  [[nodiscard]] static std::vector<Page> from_objects(Document& document,
                                                      const std::vector<Object::Reference>& pages);

  // A rectangle array normalised, or no value when it is not four numbers.
  [[nodiscard]] static std::optional<Page::Box> box_of(Document& document, const Object& value);
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_PAGE_TREE_HPP
