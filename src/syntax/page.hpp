// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Page is one leaf of the page tree as the walk recorded it, docs/DESIGN.md
// chapter 4: its number in tree order, its object, and the inheritable
// attributes of ISO 32000-1 section 7.7.3.4 (read) resolved down the tree,
// /Resources, /MediaBox, /CropBox and /Rotate. Boxes are normalised so the
// lower-left corner comes first, the crop box is clipped to the media box,
// and the rotation is one of 0, 90, 180 and 270.

#ifndef PDFKIT_SYNTAX_PAGE_HPP
#define PDFKIT_SYNTAX_PAGE_HPP

#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <cstdint>

namespace pdfkit::syntax {

class Page {
 public:
  struct Box {
    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double top = 0.0;
    friend bool operator==(const Box&, const Box&) = default;
  };

  // 1 for the first page in tree order.
  std::uint32_t number = 0;
  // The page object; generation 0 and number 0 for a page written directly in
  // its parent's /Kids.
  Object::Reference reference;
  Dictionary dictionary;
  Box media_box;
  Box crop_box;
  int rotate = 0;
  // The resolved resource dictionary, or null when the page has none.
  Object resources;
  // /Contents as written: a reference, an array of references, or null.
  Object contents;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_PAGE_HPP
