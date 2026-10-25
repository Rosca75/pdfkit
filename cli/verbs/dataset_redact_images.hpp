// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb's member for catalogue function 6: a seeded image
// treatment job per file (tests/dataset/outcome.md). Every image pdfkit_images
// lists gets a treatment in the cycle box, blur, remove from a seeded start,
// every second box with the caption REDACTED; an image that does not decode
// is removed and a bitonal image too small to blur is boxed; then one region
// box at a seeded rectangle of one kept image, in page space over one of its
// draws when it has one. The output goes to <dir>/outputs/<id>.images.pdf,
// with the write report's rectangles and image entries, and is listed again
// to name every removed or treated identity it still holds.

#ifndef PDFKIT_CLI_VERBS_DATASET_REDACT_IMAGES_HPP
#define PDFKIT_CLI_VERBS_DATASET_REDACT_IMAGES_HPP

#include "pdfkit/pdfkit.h"

#include <string>
#include <string_view>

namespace pdfkit::cli::verbs {

// The one-line JSON value of the outcome's "redact_images" for an open
// document; out_dir is the run folder.
[[nodiscard]] std::string dataset_redact_images(pdfkit_document* document, std::string_view id,
                                                const std::string& out_dir);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_DATASET_REDACT_IMAGES_HPP
