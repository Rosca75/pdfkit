// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb's member for catalogue function 2: the text of every page
// as tests/dataset/outcome.md fixes it, with the counts the reading gate
// reports (codes per decoding rung, unreadable codes, hidden glyphs per
// class, the warning codes) and the re-serialisation check of every content
// stream walk, which the gate holds at 100 percent of undamaged walks.

#ifndef PDFKIT_CLI_VERBS_DATASET_TEXT_HPP
#define PDFKIT_CLI_VERBS_DATASET_TEXT_HPP

#include "pdfkit/pdfkit.h"

#include <string>

namespace pdfkit::cli::verbs {

// The one-line JSON value of the outcome's "text" for an open document.
[[nodiscard]] std::string dataset_text(pdfkit_document* document);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_DATASET_TEXT_HPP
