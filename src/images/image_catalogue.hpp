// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageCatalogue is every image XObject a document's pages can reach through
// their resources, by identity: each page's /XObject resources and, down
// through every form XObject they hold, the forms' own (a form drawn from
// several pages is read once per page and listed once), the resources of the
// tiling patterns and of the soft-mask groups the /Pattern and /ExtGState
// resources name (an image reached only there is marked in_pattern, the
// image_in_pattern case of docs/DESIGN.md chapter 19). It is what
// pdfkit_image_data resolves an identity against. It reads resources, never
// content streams, because the one content interpreter (CLAUDE.md section
// 3.2) is what numbers occurrences; an image a page's resources hold but its
// content never draws is listed here too. Two streams with one identity are
// one entry; the first stream met is the one decoded.

#ifndef PDFKIT_IMAGES_IMAGE_CATALOGUE_HPP
#define PDFKIT_IMAGES_IMAGE_CATALOGUE_HPP

#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::images {

class ImageCatalogue {
 public:
  struct Entry {
    std::string identity;
    // The image XObject (a stream), as resolved.
    syntax::Object stream;
    // Its object number, for messages.
    std::uint32_t object_number = 0;
    // The pages whose resources reach it, ascending.
    std::vector<std::uint32_t> pages;
    // Reached only inside a pattern or a soft-mask group.
    bool in_pattern = true;
  };

  // Walks every page's resources. A resource tree nests at most 32 forms
  // deep, and a form or pattern is entered once per page and context.
  explicit ImageCatalogue(syntax::Document& document);

  [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }
  // The entry of an identity, or nullptr.
  [[nodiscard]] const Entry* find(std::string_view identity) const;

 private:
  void walk(const syntax::Object& resources, std::uint32_t page, bool in_pattern, int depth);
  void add(const syntax::Object& stream, std::uint32_t page, bool in_pattern);

  syntax::Document* document_;
  std::vector<Entry> entries_;
  std::map<std::string, std::size_t, std::less<>> by_identity_;
  // The identity of each image object number already hashed.
  std::map<std::uint32_t, std::string> identity_of_object_;
  // Per page, the resource dictionaries walked and the form, pattern and
  // group streams entered, by object number and context.
  std::set<std::pair<std::uint32_t, bool>> walked_;
  std::set<std::pair<std::uint32_t, bool>> entered_;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_IMAGE_CATALOGUE_HPP
