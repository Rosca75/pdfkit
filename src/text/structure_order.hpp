// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// StructureOrder reads a tagged PDF's reading order, docs/DESIGN.md chapter 7:
// the structure tree under the catalog's /StructTreeRoot (ISO 32000-1 section
// 14.7, read) is walked depth first through /K (an MCID integer, a marked-
// content reference /Type /MCR, a structure element, or an array of them;
// an object reference /Type /OBJR names no content and is skipped), each
// marked-content item taking its page from the nearest /Pg, and the MCIDs of
// each page are listed in tree order. The layout uses this order instead of
// its own only when every MCID the tree orders on a page resolves to glyphs
// of that page, because the tree is the producer's own statement of reading
// order and a tree that names content the page does not hold is not. The
// walk visits each object once and is bounded in depth and size, so a cyclic
// or enormous tree costs a bounded amount.

#ifndef PDFKIT_TEXT_STRUCTURE_ORDER_HPP
#define PDFKIT_TEXT_STRUCTURE_ORDER_HPP

#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace pdfkit::text {

class StructureOrder {
 public:
  // Reads document's structure tree, if it has one.
  explicit StructureOrder(syntax::Document& document);

  [[nodiscard]] bool present() const noexcept { return present_; }
  // The MCIDs the tree orders on the page with object number page, in tree
  // order; empty when it orders none.
  [[nodiscard]] const std::vector<std::int64_t>& mcids(std::uint32_t page) const;

 private:
  void visit(const syntax::Object& node, std::optional<std::uint32_t> page, int depth);

  syntax::Document* document_;
  bool present_ = false;
  std::size_t visited_count_ = 0;
  std::set<std::uint32_t> visited_;
  std::map<std::uint32_t, std::vector<std::int64_t>> pages_;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_STRUCTURE_ORDER_HPP
