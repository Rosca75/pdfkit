// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The page tree walk with inheritance, and the normalisation of boxes and
// rotation. The walk keeps an explicit stack of the nodes on the current path,
// which is both the cycle check and the bound on recursion.

#include "syntax/page_tree.hpp"

#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace pdfkit::syntax {

namespace {

// US Letter, the media box viewers assume when none is given (docs/DESIGN.md
// chapter 4).
constexpr Page::Box letter{0.0, 0.0, 612.0, 792.0};

struct Inherited {
  Object resources;
  std::optional<Page::Box> media_box;
  std::optional<Page::Box> crop_box;
  Object rotate;
};

struct Frame {
  std::optional<Object::Reference> reference;
  Inherited inherited;
  Object::Array kids;
  std::size_t next = 0;
};

Inherited inherit(Document& document, const Inherited& parent, const Dictionary& node) {
  Inherited result = parent;
  if (node.contains("Resources")) {
    const Object resources = document.resolve_entry(node, "Resources");
    if (resources.dictionary() != nullptr) {
      result.resources = resources;
    }
  }
  if (const auto box = PageTree::box_of(document, node.get("MediaBox"))) {
    result.media_box = box;
  }
  if (const auto box = PageTree::box_of(document, node.get("CropBox"))) {
    result.crop_box = box;
  }
  if (node.contains("Rotate")) {
    result.rotate = document.resolve_entry(node, "Rotate");
  }
  return result;
}

// /Rotate normalised to 0, 90, 180 or 270; a value that is not a multiple of
// 90 reads as 0, as pdf.js reads it (recalled).
int rotation_of(const Object& value) {
  const auto number = value.number();
  if (!number || !std::isfinite(*number) || std::abs(*number) > 1e9) {
    return 0;
  }
  const auto degrees = static_cast<long long>(*number);
  if (static_cast<double>(degrees) != *number || degrees % 90 != 0) {
    return 0;
  }
  return static_cast<int>(((degrees % 360) + 360) % 360);
}

Page make_page(std::uint32_t number, std::optional<Object::Reference> reference,
               const Dictionary& dictionary, const Inherited& inherited) {
  Page page;
  page.number = number;
  page.reference = reference.value_or(Object::Reference{});
  page.dictionary = dictionary;
  page.media_box = inherited.media_box.value_or(letter);
  Page::Box crop = inherited.crop_box.value_or(page.media_box);
  // The crop box is clipped to the media box (ISO 32000-1 section 14.11.2);
  // one with no overlap reads as the media box.
  crop.left = std::max(crop.left, page.media_box.left);
  crop.bottom = std::max(crop.bottom, page.media_box.bottom);
  crop.right = std::min(crop.right, page.media_box.right);
  crop.top = std::min(crop.top, page.media_box.top);
  page.crop_box = crop.right > crop.left && crop.top > crop.bottom ? crop : page.media_box;
  page.rotate = rotation_of(inherited.rotate);
  page.resources = inherited.resources;
  page.contents = dictionary.get("Contents");
  return page;
}

std::string name_of(const std::optional<Object::Reference>& reference) {
  return reference ? "object " + std::to_string(reference->number) : std::string("a direct kid");
}

}  // namespace

std::optional<Page::Box> PageTree::box_of(Document& document, const Object& value) {
  const Object resolved = document.resolve(value);
  const Object::Array* items = resolved.array();
  if (items == nullptr || items->size() < 4) {
    return std::nullopt;
  }
  std::array<double, 4> numbers{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto number = document.resolve((*items)[i]).number();
    if (!number || !std::isfinite(*number)) {
      return std::nullopt;
    }
    numbers.at(i) = *number;
  }
  return Page::Box{std::min(numbers[0], numbers[2]), std::min(numbers[1], numbers[3]),
                   std::max(numbers[0], numbers[2]), std::max(numbers[1], numbers[3])};
}

std::vector<Page> PageTree::walk(Document& document) {
  std::vector<Page> pages;
  const Object catalog = document.catalog();
  const Dictionary* catalog_dictionary = catalog.dictionary();
  if (catalog_dictionary == nullptr) {
    return pages;
  }
  const std::uint64_t ceiling = document.limits().page_ceiling;
  // A self-multiplying tree of empty nodes yields no pages to count against
  // the ceiling, so the visits are bounded too.
  const std::uint64_t most_visits = ceiling * 4 + 100000;
  std::uint64_t visits = 0;
  std::vector<Frame> stack;
  Frame root;
  root.kids.push_back(catalog_dictionary->get("Pages"));
  stack.push_back(std::move(root));
  while (!stack.empty()) {
    Frame& top = stack.back();
    if (top.next >= top.kids.size()) {
      stack.pop_back();
      continue;
    }
    const Object kid = top.kids[top.next++];
    if (++visits > most_visits) {
      document.repair("the page tree holds more than " + std::to_string(most_visits) +
                      " nodes; the walk stops there");
      break;
    }
    const std::optional<Object::Reference> reference = kid.reference();
    if (reference && std::ranges::any_of(stack, [&](const Frame& frame) {
          return frame.reference && frame.reference->number == reference->number;
        })) {
      document.repair("the page tree holds a cycle at " + name_of(reference) +
                      ", which is skipped");
      continue;
    }
    const Object node = document.resolve(kid);
    const Dictionary* dictionary = node.dictionary();
    if (dictionary == nullptr) {
      document.repair("a /Kids entry (" + name_of(reference) +
                      ") names no dictionary and is skipped");
      continue;
    }
    Inherited inherited = inherit(document, top.inherited, *dictionary);
    const Object& type = dictionary->get("Type");
    const Object kids = document.resolve_entry(*dictionary, "Kids");
    // A node is /Type /Pages, or an untyped object with /Kids (pdf.js's reading).
    const bool is_node =
        type.is_name("Pages") || (!type.is_name("Page") && kids.array() != nullptr);
    if (is_node) {
      Frame frame;
      frame.reference = reference;
      frame.inherited = std::move(inherited);
      if (const Object::Array* list = kids.array()) {
        frame.kids = *list;
      }
      stack.push_back(std::move(frame));
      continue;
    }
    if (pages.size() >= ceiling) {
      document.limits().refuse_page_ceiling(pages.size() + 1);
    }
    pages.push_back(
        make_page(static_cast<std::uint32_t>(pages.size() + 1), reference, *dictionary, inherited));
  }
  return pages;
}

std::vector<Page> PageTree::from_objects(Document& document,
                                         const std::vector<Object::Reference>& references) {
  std::vector<Page> pages;
  for (const Object::Reference reference : references) {
    const Object node = document.object(reference);
    if (const Dictionary* dictionary = node.dictionary()) {
      const Inherited inherited = inherit(document, Inherited{}, *dictionary);
      pages.push_back(make_page(static_cast<std::uint32_t>(pages.size() + 1), reference,
                                *dictionary, inherited));
    }
  }
  return pages;
}

}  // namespace pdfkit::syntax
