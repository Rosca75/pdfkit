// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Resources looks up a content stream's named resources by category (/Font,
// /XObject, /ExtGState, /ColorSpace, /Pattern, /Shading, /Properties), docs/
// DESIGN.md chapter 4: a form XObject with no /Resources falls back to the
// resources of whatever draws it (the page or the enclosing form), which the
// specification deprecates and every viewer tolerates; a name that resolves
// to nothing is the warning resource_missing, naming the page, and never a
// refusal.

#ifndef PDFKIT_SYNTAX_RESOURCES_HPP
#define PDFKIT_SYNTAX_RESOURCES_HPP

#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <cstdint>
#include <string_view>

namespace pdfkit::syntax {

class Document;

class Resources {
 public:
  // dictionary is a resource dictionary or a reference to one (null for none);
  // page is the page the content draws on, for warnings. document must
  // outlive the lookup.
  Resources(Document& document, const Object& dictionary, std::uint32_t page);

  // The resources of a form XObject drawn with these: its own /Resources, or
  // these when it has none.
  [[nodiscard]] Resources for_form(const Dictionary& form) const;

  // The resolved resource named name in category, or null with a
  // resource_missing warning when nothing answers.
  [[nodiscard]] Object lookup(std::string_view category, std::string_view name) const;

  // The resolved resource dictionary; null when there is none.
  [[nodiscard]] const Object& dictionary() const noexcept { return dictionary_; }

 private:
  Document* document_;
  Object dictionary_;
  std::uint32_t page_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_RESOURCES_HPP
