// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The resource walk of ImageCatalogue. A resource dictionary and a form,
// pattern or group stream are walked once per page and per context (normal or
// inside a pattern), so a page's list of images is complete and the walk stays
// linear in the resources however the forms share one another.

#include "images/image_catalogue.hpp"

#include "images/image_identity.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/stream.hpp"

#include <algorithm>

namespace pdfkit::images {

namespace {

constexpr int max_depth = 32;

}  // namespace

ImageCatalogue::ImageCatalogue(syntax::Document& document) : document_(&document) {
  for (const syntax::Page& page : document.pages()) {
    walked_.clear();
    entered_.clear();
    walk(page.resources, page.number, false, 0);
  }
}

const ImageCatalogue::Entry* ImageCatalogue::find(std::string_view identity) const {
  const auto found = by_identity_.find(identity);
  return found != by_identity_.end() ? &entries_[found->second] : nullptr;
}

void ImageCatalogue::walk(const syntax::Object& resources, std::uint32_t page, bool in_pattern,
                          int depth) {
  if (depth > max_depth) {
    return;
  }
  const syntax::Object value = document_->resolve(resources);
  const syntax::Dictionary* dictionary = value.dictionary();
  if (dictionary == nullptr) {
    return;
  }
  // A resource dictionary written by reference is walked once per page and
  // context.
  if (const std::optional<syntax::Object::Reference> reference = resources.reference()) {
    if (!walked_.emplace(reference->number, in_pattern).second) {
      return;
    }
  }
  const auto each = [&](std::string_view category, const auto& visit) {
    const syntax::Object group = document_->resolve_entry(*dictionary, category);
    if (const syntax::Dictionary* entries = group.dictionary()) {
      for (const auto& [name, item] : entries->entries()) {
        visit(item);
      }
    }
  };
  // Enters a form, pattern or group stream's own resources.
  const auto enter = [&](const syntax::Object& item, bool pattern) {
    const syntax::Object resolved = document_->resolve(item);
    const syntax::Stream* stream = resolved.stream();
    if (stream == nullptr) {
      return;
    }
    // A stream already entered on this page in this context adds nothing, and
    // entering it once is what keeps a cycle, or a tree of forms sharing
    // forms, from multiplying the walk.
    if (!entered_.emplace(stream->owner().number, pattern).second) {
      return;
    }
    walk(stream->dictionary().get("Resources"), page, pattern, depth + 1);
  };
  each("XObject", [&](const syntax::Object& item) {
    const syntax::Object resolved = document_->resolve(item);
    const syntax::Stream* stream = resolved.stream();
    if (stream == nullptr) {
      return;
    }
    const syntax::Object subtype = document_->resolve_entry(stream->dictionary(), "Subtype");
    if (subtype.is_name("Image")) {
      add(resolved, page, in_pattern);
    } else if (subtype.is_name("Form")) {
      enter(item, in_pattern);
    }
  });
  each("Pattern", [&](const syntax::Object& item) { enter(item, true); });
  each("ExtGState", [&](const syntax::Object& item) {
    const syntax::Object state = document_->resolve(item);
    if (const syntax::Dictionary* entries = state.dictionary()) {
      const syntax::Object mask = document_->resolve_entry(*entries, "SMask");
      if (const syntax::Dictionary* soft = mask.dictionary()) {
        enter(soft->get("G"), true);
      }
    }
  });
}

void ImageCatalogue::add(const syntax::Object& stream, std::uint32_t page, bool in_pattern) {
  const syntax::Stream& image = *stream.stream();
  const std::uint32_t number = image.owner().number;
  auto known = identity_of_object_.find(number);
  if (known == identity_of_object_.end()) {
    known = identity_of_object_.emplace(number, ImageIdentity::of_stream(*document_, image)).first;
  }
  const std::string& identity = known->second;
  auto found = by_identity_.find(identity);
  if (found == by_identity_.end()) {
    Entry entry;
    entry.identity = identity;
    entry.stream = stream;
    entry.object_number = number;
    found = by_identity_.emplace(identity, entries_.size()).first;
    entries_.push_back(std::move(entry));
  }
  Entry& entry = entries_[found->second];
  if (entry.pages.empty() || entry.pages.back() != page) {
    entry.pages.push_back(page);
  }
  entry.in_pattern = entry.in_pattern && in_pattern;
}

}  // namespace pdfkit::images
