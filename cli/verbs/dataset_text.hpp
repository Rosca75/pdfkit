// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb's member for catalogue function 2: the text of every page
// as tests/dataset/outcome.md fixes it, with the counts the reading gate
// reports (codes per decoding rung, unreadable codes, hidden glyphs per
// class, the warning codes), the re-serialisation check of every content
// stream walk, which the gate holds at 100 percent of undamaged walks, and
// the pages whose text could not be read in full.

#ifndef PDFKIT_CLI_VERBS_DATASET_TEXT_HPP
#define PDFKIT_CLI_VERBS_DATASET_TEXT_HPP

#include "pdfkit/pdfkit.h"

#include "json_text.hpp"

#include <string>

namespace pdfkit::cli::verbs {

// The one-line JSON value of the outcome's "text" for an open document.
[[nodiscard]] std::string dataset_text(pdfkit_document* document);

// The page numbers of an answer's damaged_pages member as a JSON array, in
// the answer's order; [] when the answer names none.
[[nodiscard]] std::string damaged_page_numbers(const JsonText& answer);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_DATASET_TEXT_HPP
