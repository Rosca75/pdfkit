// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The budget a document's limits name, and the refusals past the page ceiling
// and the operator count, each with the count, the limit and the way out.

#include "syntax/limits.hpp"

#include "platform/error.hpp"

#include <string>

namespace pdfkit::syntax {

platform::Budget::Limits Limits::budget() const {
  if (page_ceiling == 0 || operators_per_page == 0) {
    throw platform::Error(platform::ErrorCode::invalid_argument, "a limit was set to zero",
                          "page_ceiling and operators_per_page each at least 1",
                          "leave a limit out of the open options to use its default, or give a "
                          "positive value");
  }
  return {stream_bytes, document_bytes, objects};
}

void Limits::refuse_page_ceiling(std::uint64_t pages) const {
  throw platform::Error(
      platform::ErrorCode::page_ceiling,
      "the document has more than " + std::to_string(page_ceiling) + " pages (at least " +
          std::to_string(pages) + " found)",
      "at most " + std::to_string(page_ceiling) + " pages, the page ceiling of the open options",
      "split the file with its producing application, or raise page_ceiling in "
      "the open options");
}

void Limits::refuse_operators(std::string_view what) const {
  throw platform::Error(platform::ErrorCode::limit_exceeded,
                        std::string(what) + " holds more than " +
                            std::to_string(operators_per_page) + " content operators",
                        "at most " + std::to_string(operators_per_page) +
                            " operators per page, the limit of the open options",
                        "re-save or split the file with its producing application, or raise "
                        "operators_per_page in the open options");
}

}  // namespace pdfkit::syntax
