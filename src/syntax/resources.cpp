// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Resource lookup and the form fallback. The document is held by pointer
// because a Resources value is copied into every form it reaches; it never
// owns the document.

#include "syntax/resources.hpp"

#include "syntax/document.hpp"

#include <string>

namespace pdfkit::syntax {

Resources::Resources(Document& document, const Object& dictionary, std::uint32_t page)
    : document_(&document), dictionary_(document.resolve(dictionary)), page_(page) {
  if (dictionary_.dictionary() == nullptr) {
    dictionary_ = Object();
  }
}

Resources Resources::for_form(const Dictionary& form) const {
  const Object own = document_->resolve_entry(form, "Resources");
  if (own.dictionary() != nullptr) {
    return {*document_, own, page_};
  }
  return *this;
}

Object Resources::lookup(std::string_view category, std::string_view name) const {
  if (const Dictionary* resources = dictionary_.dictionary()) {
    const Object group = document_->resolve_entry(*resources, category);
    if (const Dictionary* entries = group.dictionary()) {
      const Object value = document_->resolve_entry(*entries, name);
      if (!value.is_null()) {
        return value;
      }
    }
  }
  document_->warn("resource_missing",
                  "page " + std::to_string(page_) + " names the resource /" + std::string(name) +
                      " in /" + std::string(category) +
                      ", which resolves to nothing; the operator draws nothing",
                  page_);
  return {};
}

}  // namespace pdfkit::syntax
