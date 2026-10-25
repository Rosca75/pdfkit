// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// EditPlan is the state of an edit handle (CLAUDE.md section 4): what the
// caller asked to change in one document, held until pdfkit_write applies it
// through the write pipeline. It holds the page selection, a validated list
// of pages in the caller's order, each named once. The plan grows with the
// edits the catalogue adds, each a member of its own:
//   - the text matches with their placeholders (docs/DESIGN.md sections 11
//     and 12), added by pdfkit_redact_text;
//   - the image treatments per image identity (section 14), added by
//     pdfkit_redact_images;
//   - the metadata field values (section 10), added by pdfkit_set_metadata.

#ifndef PDFKIT_REDACT_EDIT_PLAN_HPP
#define PDFKIT_REDACT_EDIT_PLAN_HPP

#include "redact/text_plan.hpp"
#include "syntax/document.hpp"
#include "writer/metadata_writer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::redact {

class EditPlan {
 public:
  // document must outlive the plan.
  explicit EditPlan(syntax::Document& document) : document_(&document) {}

  [[nodiscard]] syntax::Document& document() noexcept { return *document_; }

  // Records the pages to keep, in this order. Throws platform::Error
  // (invalid_argument) for an empty list, a page named twice or a page out of
  // range, naming the page and the document's page count.
  void select_pages(const std::vector<std::int64_t>& pages);

  // The pages to keep, 1-based, in output order; no value keeps every page.
  [[nodiscard]] const std::optional<std::vector<std::uint32_t>>& pages() const noexcept {
    return pages_;
  }

  // The text matches and the work they give each page, which
  // pdfkit_redact_text fills (redact::TextRedactor).
  [[nodiscard]] TextPlan& text() noexcept { return text_; }
  [[nodiscard]] const TextPlan& text() const noexcept { return text_; }

  // The document fingerprint a glyph range names: the SHA-256 of the input
  // bytes in lower-case hexadecimal, computed on first use.
  [[nodiscard]] const std::string& fingerprint();

  // The metadata values pdfkit_set_metadata recorded, by field; a later
  // value of a field replaces an earlier one.
  [[nodiscard]] writer::MetadataWriter::Values& metadata() noexcept { return metadata_; }
  [[nodiscard]] const writer::MetadataWriter::Values& metadata() const noexcept {
    return metadata_;
  }
  // The source's values of every field set, which the self-check searches
  // the output for: each Info value and each paired XMP value and item.
  [[nodiscard]] std::vector<std::string>& metadata_originals() noexcept {
    return metadata_originals_;
  }
  [[nodiscard]] const std::vector<std::string>& metadata_originals() const noexcept {
    return metadata_originals_;
  }

 private:
  syntax::Document* document_;
  std::optional<std::vector<std::uint32_t>> pages_;
  TextPlan text_;
  std::string fingerprint_;
  writer::MetadataWriter::Values metadata_;
  std::vector<std::string> metadata_originals_;
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_EDIT_PLAN_HPP
