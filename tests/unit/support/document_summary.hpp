// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The helpers the document tests share: opening builder output, and a text
// summary of what a document reads as (its pages, their boxes, rotation and
// decoded content, and the catalog's and information dictionary's keys), so
// a test of reconstruction can state that a damaged file reads as its clean
// source by comparing two summaries.

#ifndef PDFKIT_TESTS_SUPPORT_DOCUMENT_SUMMARY_HPP
#define PDFKIT_TESTS_SUPPORT_DOCUMENT_SUMMARY_HPP

#include "syntax/document.hpp"
#include "syntax/limits.hpp"

#include <memory>
#include <string>

namespace pdfkit::test {

// Opens file with limits (the defaults when not given).
[[nodiscard]] std::unique_ptr<syntax::Document> open_document(const std::string& file,
                                                              const syntax::Limits& limits = {});

// What the document reads as, one line per fact.
[[nodiscard]] std::string summarise(syntax::Document& document);

// The decoded bytes of page number (1-based), its pieces joined.
[[nodiscard]] std::string page_text(syntax::Document& document, std::uint32_t number);

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_SUPPORT_DOCUMENT_SUMMARY_HPP
