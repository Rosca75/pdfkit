// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The page list's checks. Each refusal names what was wrong, the range the
// document allows and how to fix the list, because a caller building a list
// from another tool's page numbers needs to see which one was off.

#include "redact/edit_plan.hpp"

#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"

#include <set>
#include <string>
#include <utility>

namespace pdfkit::redact {

void EditPlan::select_pages(const std::vector<std::int64_t>& pages) {
  const std::size_t count = document_->pages().size();
  const std::string range = "page numbers from 1 to " + std::to_string(count);
  if (pages.empty()) {
    throw platform::Error(platform::ErrorCode::invalid_argument,
                          "pdfkit_select_pages was given an empty page list",
                          "at least one page to keep, " + range,
                          "list the pages to keep, for example {\"pages\": [1]}");
  }
  std::set<std::int64_t> seen;
  std::vector<std::uint32_t> kept;
  kept.reserve(pages.size());
  for (const std::int64_t page : pages) {
    if (page < 1 || std::cmp_greater(page, count)) {
      throw platform::Error(
          platform::ErrorCode::invalid_argument,
          "pdfkit_select_pages was given page " + std::to_string(page) + ", but the document has " +
              std::to_string(count) + " page" + (count == 1 ? "" : "s"),
          range, "remove page " + std::to_string(page) + " from the list or check the document");
    }
    if (!seen.insert(page).second) {
      throw platform::Error(
          platform::ErrorCode::invalid_argument,
          "pdfkit_select_pages was given page " + std::to_string(page) + " more than once",
          "each page named once, " + range,
          "remove the repeated " + std::to_string(page) + " from the list");
    }
    kept.push_back(static_cast<std::uint32_t>(page));
  }
  pages_ = std::move(kept);
}

const std::string& EditPlan::fingerprint() {
  if (fingerprint_.empty()) {
    const auto digest =
        platform::crypto::Sha2::digest(platform::crypto::Sha2::Variant::sha256, document_->bytes());
    constexpr std::string_view hex = "0123456789abcdef";
    for (const std::byte value : digest) {
      const auto byte = std::to_integer<unsigned>(value);
      fingerprint_ += hex[byte >> 4U];
      fingerprint_ += hex[byte & 0x0FU];
    }
  }
  return fingerprint_;
}

}  // namespace pdfkit::redact
