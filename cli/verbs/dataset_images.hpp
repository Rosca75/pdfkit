// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The images section of the dataset verb's outcome (tests/dataset/outcome.md):
// the image count pdfkit_images lists for the whole document, how many decode,
// and the identity and code of each that does not.

#ifndef PDFKIT_CLI_VERBS_DATASET_IMAGES_HPP
#define PDFKIT_CLI_VERBS_DATASET_IMAGES_HPP

#include "pdfkit/pdfkit.h"

#include <string>

namespace pdfkit::cli::verbs {

// The section's JSON object for an open document.
[[nodiscard]] std::string dataset_images(pdfkit_document* document);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_DATASET_IMAGES_HPP
