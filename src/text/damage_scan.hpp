// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// DamageScan answers which pages of a document could not be read in full,
// docs/DESIGN.md (the damaged pages section): it runs the content
// interpreter, the one walk that reads every page piece and form, over each
// page asked and keeps each page's answer, so a later call reads the same
// answer whatever was called before and no second decoder decides it. A
// limit met while a page is read names that page not_checked rather than
// failing, because the calls that ask for every page (inspect, the write
// report) never fail for one page. A scan belongs to one document handle and
// is used by one thread at a time (CLAUDE.md section 3.3).

#ifndef PDFKIT_TEXT_DAMAGE_SCAN_HPP
#define PDFKIT_TEXT_DAMAGE_SCAN_HPP

#include "syntax/document.hpp"
#include "syntax/page_damage.hpp"
#include "text/page_content.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace pdfkit::text {

class DamageScan {
 public:
  // document must outlive the scan.
  explicit DamageScan(syntax::Document& document) : document_(&document) {}

  // The damaged pages among numbers (1-based, within the document), in the
  // order given, each read once over the scan's life.
  [[nodiscard]] std::vector<syntax::PageDamage> pages(std::span<const std::uint32_t> numbers);
  // The damaged pages of the document, in page order.
  [[nodiscard]] std::vector<syntax::PageDamage> all();

  // Keeps the answer a call that read page content itself found, so the
  // scan does not read the page again.
  void record(const PageContent& content);

 private:
  syntax::Document* document_;
  // Per page read, its damage or no value for a page read in full.
  std::map<std::uint32_t, std::optional<syntax::PageDamage>> read_;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_DAMAGE_SCAN_HPP
