// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb's members for catalogue functions 4 and 7
// (tests/dataset/outcome.md): the metadata read side by side, and a job that
// rewrites every field the file holds to "REDACTED <FIELD>" and searches the
// page text for every original value of those fields, because the
// self-check refuses an output in which an original survives anywhere; the
// output goes to <dir>/outputs/<id>.metadata.pdf and is read back to check
// that both places carry the written values. The text redaction job of
// dataset_redact.cpp rewrites the fields that hold one of its words.

#ifndef PDFKIT_CLI_VERBS_DATASET_METADATA_HPP
#define PDFKIT_CLI_VERBS_DATASET_METADATA_HPP

#include "pdfkit/pdfkit.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::cli::verbs {

struct DatasetMetadata {
  // The one-line JSON value of the outcome's "metadata".
  std::string member;
  // Each field the file holds in either place, with its values: the Info
  // value, each paired XMP value and each item of an XMP array.
  std::vector<std::pair<std::string, std::vector<std::string>>> fields;
};

// Reads the metadata of an open document.
[[nodiscard]] DatasetMetadata dataset_metadata(pdfkit_document* document);

// The fields_json of pdfkit_set_metadata rewriting every field of metadata
// one of whose values holds one of strings (ASCII letters compared without
// case), each to "REDACTED <FIELD>"; empty when none does. names receives the
// fields.
[[nodiscard]] std::string dataset_metadata_rewrite(const DatasetMetadata& metadata,
                                                   const std::vector<std::string>& strings,
                                                   std::vector<std::string>& names);

// The one-line JSON value of the outcome's "set_metadata" for an open
// document; out_dir is the run folder.
[[nodiscard]] std::string dataset_set_metadata(pdfkit_document* document, std::string_view id,
                                               const DatasetMetadata& metadata,
                                               const std::string& out_dir);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_DATASET_METADATA_HPP
