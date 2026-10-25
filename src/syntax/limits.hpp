// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Limits are the open options that bound what one document may cost, docs/
// DESIGN.md section 3.2: the decoded bytes per stream and per document, the
// object count, the page ceiling and the content operators per page. They are
// bound into one platform::Budget per document. The refusals past the page
// ceiling and the operator count are built here, so every message names the
// count, the limit and the way out the same way.

#ifndef PDFKIT_SYNTAX_LIMITS_HPP
#define PDFKIT_SYNTAX_LIMITS_HPP

#include "platform/budget.hpp"

#include <cstdint>
#include <string_view>

namespace pdfkit::syntax {

class Limits {
 public:
  // The defaults of docs/DESIGN.md section 3.2 (the ceilings are author decisions).
  std::uint64_t stream_bytes = 512ULL * 1024 * 1024;
  std::uint64_t document_bytes = 2048ULL * 1024 * 1024;
  std::uint64_t objects = 8388607;
  std::uint64_t page_ceiling = 10000;
  std::uint64_t operators_per_page = 50000000;

  // The budget limits these name; throws invalid_argument for a zero limit.
  [[nodiscard]] platform::Budget::Limits budget() const;

  // The page_ceiling refusal for a document of at least pages pages.
  [[noreturn]] void refuse_page_ceiling(std::uint64_t pages) const;

  // The limit_exceeded refusal for a content stream of more operators than
  // operators_per_page; what names the stream, such as "page 3".
  [[noreturn]] void refuse_operators(std::string_view what) const;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_LIMITS_HPP
