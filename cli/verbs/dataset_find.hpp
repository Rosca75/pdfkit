// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb's member for catalogue function 11: the redaction job's
// seeded words (dataset_redaction_words and the trap marker) through
// pdfkit_find, the same words through pdfkit_redact_text on a fresh edit,
// and whether the two name the same glyph ranges; then the first occurrence
// of the first values found, each sent back as a located match with the
// fingerprint on a fresh edit, written, and the output's page searched again
// for the value, which must hold one occurrence fewer. The time of the find
// call and of the text member's call go to <dir>/outputs/<id>.find-time.json,
// beside the outcome and never in it, because an outcome holds no time.

#ifndef PDFKIT_CLI_VERBS_DATASET_FIND_HPP
#define PDFKIT_CLI_VERBS_DATASET_FIND_HPP

#include "pdfkit/pdfkit.h"

#include <string>
#include <string_view>

namespace pdfkit::cli::verbs {

// The one-line JSON value of the outcome's "find" for an open document
// whose text member is text_member; text_seconds is the time the text
// member took and out_dir the run folder.
[[nodiscard]] std::string dataset_find(pdfkit_document* document, std::string_view id,
                                       const std::string& text_member, double text_seconds,
                                       const std::string& out_dir);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_DATASET_FIND_HPP
