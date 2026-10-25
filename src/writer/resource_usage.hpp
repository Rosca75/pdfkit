// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ResourceUsage reads which resource names a content stream uses, per
// category of the resource dictionary (ISO 32000-1 section 7.8.3 and the
// operator tables of Annex A, read): /Font from Tf, /XObject from Do,
// /ExtGState from gs, /ColorSpace from cs and CS and from an inline image's
// /ColorSpace (or /CS), /Pattern from the name operand of scn and SCN,
// /Shading from sh, /Properties from the property-list name of BDC and DP. It
// reads through the content lexer of the syntax layer, the one reader of
// content streams. A stream the lexer had to recover in, or one that did not
// decode to its end, is incomplete: its user keeps the whole resource
// dictionary, because pruning a name the stream uses breaks the page.

#ifndef PDFKIT_WRITER_RESOURCE_USAGE_HPP
#define PDFKIT_WRITER_RESOURCE_USAGE_HPP

#include "syntax/limits.hpp"

#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::writer {

class ResourceUsage {
 public:
  // The resource categories pruning knows; every other key of a resource
  // dictionary except /ProcSet is dropped from a pruned copy.
  static constexpr std::array<std::string_view, 7> categories{
      "Font", "XObject", "ExtGState", "ColorSpace", "Pattern", "Shading", "Properties"};

  // Category to the names used in it.
  std::map<std::string, std::set<std::string>, std::less<>> names;
  // False when a name may have been missed.
  bool complete = true;

  // The names pieces (a page's /Contents pieces or a form's one stream) use;
  // what names the content in a refusal ("page 3").
  [[nodiscard]] static ResourceUsage of(const std::vector<std::vector<std::byte>>& pieces,
                                        const syntax::Limits& limits, std::string what);

  // Adds every name of other, and its incompleteness.
  void merge(const ResourceUsage& other);
  // Whether category holds name.
  [[nodiscard]] bool uses(std::string_view category, std::string_view name) const;
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_RESOURCE_USAGE_HPP
