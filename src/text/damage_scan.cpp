// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The scan reads the pages it has no answer for through one text reader per
// call, which shares the font cache and the decoded forms across those
// pages and is dropped when the call returns, so the scan keeps only the
// answers. A limit (the operator count, a stream or the document past its
// bytes) names the page not_checked with the limit's own message; any other
// failure is the document's and passes through.

#include "text/damage_scan.hpp"

#include "platform/error.hpp"
#include "text/text_reader.hpp"

#include <memory>
#include <utility>

namespace pdfkit::text {

std::vector<syntax::PageDamage> DamageScan::pages(std::span<const std::uint32_t> numbers) {
  std::unique_ptr<TextReader> reader;
  std::vector<syntax::PageDamage> out;
  for (const std::uint32_t number : numbers) {
    if (!read_.contains(number)) {
      if (reader == nullptr) {
        reader = std::make_unique<TextReader>(*document_);
      }
      try {
        read_[number] = reader->interpret(number).damaged_page();
      } catch (const platform::Error& error) {
        if (error.code() != platform::ErrorCode::limit_exceeded) {
          throw;
        }
        read_[number] = syntax::PageDamage::unchecked(number, error);
      }
    }
    if (const std::optional<syntax::PageDamage>& damage = read_.at(number)) {
      out.push_back(*damage);
    }
  }
  return out;
}

std::vector<syntax::PageDamage> DamageScan::all() {
  std::vector<std::uint32_t> numbers;
  numbers.reserve(document_->pages().size());
  for (const syntax::Page& page : document_->pages()) {
    numbers.push_back(page.number);
  }
  return pages(numbers);
}

void DamageScan::record(const PageContent& content) {
  read_.try_emplace(content.page, content.damaged_page());
}

}  // namespace pdfkit::text
