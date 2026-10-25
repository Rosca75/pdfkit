// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The structure-tree walk. The bounds are author decisions: 256 levels (a
// reading order deeper than that is not a document's), and 1,000,000 nodes.

#include "text/structure_order.hpp"

#include "syntax/dictionary.hpp"

namespace pdfkit::text {

namespace {

constexpr int max_depth = 256;
constexpr std::size_t max_nodes = 1'000'000;

}  // namespace

StructureOrder::StructureOrder(syntax::Document& document) : document_(&document) {
  const syntax::Object catalog = document.catalog();
  const syntax::Dictionary* root = catalog.dictionary();
  if (root == nullptr) {
    return;
  }
  const syntax::Object tree = document.resolve_entry(*root, "StructTreeRoot");
  const syntax::Dictionary* entries = tree.dictionary();
  if (entries == nullptr) {
    return;
  }
  present_ = true;
  if (const syntax::Object* kids = entries->find("K")) {
    visit(*kids, std::nullopt, 0);
  }
}

const std::vector<std::int64_t>& StructureOrder::mcids(std::uint32_t page) const {
  static const std::vector<std::int64_t> none;
  const auto found = pages_.find(page);
  return found == pages_.end() ? none : found->second;
}

void StructureOrder::visit(const syntax::Object& node, std::optional<std::uint32_t> page,
                           int depth) {
  if (depth > max_depth || ++visited_count_ > max_nodes) {
    return;
  }
  if (const auto reference = node.reference()) {
    if (!visited_.insert(reference->number).second) {
      return;
    }
  }
  const syntax::Object value = document_->resolve(node);
  if (const auto mcid = value.integer()) {
    if (page) {
      pages_[*page].push_back(*mcid);
    }
    return;
  }
  if (const syntax::Object::Array* items = value.array()) {
    for (const syntax::Object& item : *items) {
      visit(item, page, depth + 1);
    }
    return;
  }
  const syntax::Dictionary* entries = value.dictionary();
  if (entries == nullptr) {
    return;
  }
  if (const syntax::Object* pg = entries->find("Pg")) {
    if (const auto reference = pg->reference()) {
      page = reference->number;
    }
  }
  const syntax::Object type = document_->resolve_entry(*entries, "Type");
  if (type.is_name("OBJR")) {
    return;
  }
  if (type.is_name("MCR")) {
    // A reference into another stream (/Stm) orders content outside the page's
    // own /Contents, which the page's MCIDs do not name.
    if (entries->contains("Stm")) {
      return;
    }
    const auto mcid = document_->resolve_entry(*entries, "MCID").integer();
    if (mcid && page) {
      pages_[*page].push_back(*mcid);
    }
    return;
  }
  if (const syntax::Object* kids = entries->find("K")) {
    visit(*kids, page, depth + 1);
  }
}

}  // namespace pdfkit::text
