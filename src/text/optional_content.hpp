// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// OptionalContent answers whether the default configuration hides an
// optional-content group or membership dictionary, ISO 32000-1 section 8.11
// (read): the catalog's /OCProperties /D gives /BaseState (ON by default),
// then /ON and /OFF lists; a group is OFF when the resulting state says so.
// A membership dictionary (/Type /OCMD) is hidden by its /VE expression when
// it has one (/Not, /And, /Or over groups, nesting bounded), else by its /P
// policy over /OCGs (AnyOn by default, AllOn, AnyOff, AllOff). A group
// written as a direct object, which no /ON or /OFF list can name, is ON. The
// hidden-text class optional_content_off (docs/DESIGN.md chapter 8) is this
// answer.

#ifndef PDFKIT_TEXT_OPTIONAL_CONTENT_HPP
#define PDFKIT_TEXT_OPTIONAL_CONTENT_HPP

#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <cstdint>
#include <set>

namespace pdfkit::text {

class OptionalContent {
 public:
  // Reads the default configuration of document's catalog.
  explicit OptionalContent(syntax::Document& document);

  // Whether value (a reference to, or a dictionary of, a group or a
  // membership dictionary) is hidden by the default configuration.
  [[nodiscard]] bool hidden(const syntax::Object& value) const;

  [[nodiscard]] bool any() const noexcept { return present_; }

 private:
  [[nodiscard]] bool group_off(const syntax::Object& value) const;
  [[nodiscard]] bool expression_off(const syntax::Object& value, int depth) const;

  syntax::Document* document_;
  bool present_ = false;
  bool base_off_ = false;
  std::set<std::uint32_t> on_;
  std::set<std::uint32_t> off_;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_OPTIONAL_CONTENT_HPP
