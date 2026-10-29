// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The helpers the redaction tests share: a redaction of a fixture file
// through the C ABI (open, edit, pdfkit_redact_text, an optional page
// selection, pdfkit_write), which returns the statuses, the JSON answers and
// the output, and the reading of an output back through the text layer, so
// a test asserts on what an extractor reads from the written file.

#ifndef PDFKIT_TESTS_UNIT_REDACT_SUPPORT_REDACTION_HPP
#define PDFKIT_TESTS_UNIT_REDACT_SUPPORT_REDACTION_HPP

#include "pdfkit/pdfkit.h"

#include "text/page_text.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::test {

struct Redaction {
  pdfkit_status match_status = PDFKIT_OK;
  std::string matches;
  pdfkit_status write_status = PDFKIT_OK;
  std::string report;
  std::string pdf;
};

// Redacts file with matches_json (the argument of pdfkit_redact_text);
// pages, when not empty, is a page selection.
[[nodiscard]] Redaction redact(const std::string& file, const std::string& matches_json,
                               const std::vector<std::uint32_t>& pages = {});

// The text of page number of output (hidden blocks included), as pdfkit_text
// writes it.
[[nodiscard]] std::string page_text(const std::string& output, std::uint32_t number = 1);

// Every page of output read through the text layer.
[[nodiscard]] std::vector<text::PageText> read_pages(const std::string& output);

// The folding member with every flag on, which the C ABI folds only when
// asked.
inline constexpr std::string_view every_folding =
    R"("folding": {"case": true, "diacritics": true, "whitespace": true, "hyphenation": true})";

// A search match of text with every folding on and the placeholder.
[[nodiscard]] std::string search(const std::string& text,
                                 const std::string& placeholder = "[REDACTED]");

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_UNIT_REDACT_SUPPORT_REDACTION_HPP
