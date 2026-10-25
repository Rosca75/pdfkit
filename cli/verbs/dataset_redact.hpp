// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb's member for catalogue function 5 and the self-check of
// 10.1: a seeded redaction job per file (tests/dataset/outcome.md), five
// words of four or more letters drawn from pdfkit's own text of the file
// (hidden text included) and the marker every synthesised redaction trap
// carries, each searched with the default folding and replaced by
// [REDACTED]; the output written to <dir>/outputs/<id>.redact.pdf with the
// rectangles, the placeholders drawn per page, the removed codes and the
// output opened again.

#ifndef PDFKIT_CLI_VERBS_DATASET_REDACT_HPP
#define PDFKIT_CLI_VERBS_DATASET_REDACT_HPP

#include "pdfkit/pdfkit.h"

#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::cli::verbs {

// The marker of the synthesised redaction traps.
inline constexpr std::string_view dataset_redaction_marker = "Zebulon Quarterfield";

// The words the job of id redacts out of text (the pages' text joined).
[[nodiscard]] std::vector<std::string> dataset_redaction_words(std::string_view id,
                                                              std::string_view text);

// The one-line JSON value of the outcome's "redact_text" for an open
// document whose text member is text_member; out_dir is the run folder.
[[nodiscard]] std::string dataset_redact(pdfkit_document* document, std::string_view id,
                                         const std::string& text_member,
                                         const std::string& out_dir);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_DATASET_REDACT_HPP
