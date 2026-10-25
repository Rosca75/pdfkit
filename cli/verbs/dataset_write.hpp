// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb's members for catalogue functions 8, 9 and 10.1: the
// channels present, the identity write and a seeded page selection, each
// written to the outcome as tests/dataset/outcome.md fixes, the outputs going
// to <dir>/outputs/ under names relative to <dir>, so two runs over one file
// write the same bytes.

#ifndef PDFKIT_CLI_VERBS_DATASET_WRITE_HPP
#define PDFKIT_CLI_VERBS_DATASET_WRITE_HPP

#include "pdfkit/pdfkit.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::cli::verbs {

// One-line JSON values of the outcome's "channels", "write" and "select".
struct DatasetWrite {
  std::string channels;
  std::string write;
  std::string select;
};

// The members for an open document of pages pages; out_dir is the run folder.
[[nodiscard]] DatasetWrite dataset_write(pdfkit_document* document, std::string_view id,
                                         std::uint64_t pages, const std::string& out_dir);

// The members of a file open did not open.
[[nodiscard]] DatasetWrite dataset_write_skipped();

// The pages the selection keeps for id out of pages, in output order
// (tests/dataset/outcome.md section 2).
[[nodiscard]] std::vector<std::uint64_t> dataset_selection(std::string_view id,
                                                           std::uint64_t pages);

// The outcome of opening an output again: status, code, pages, repairs.
[[nodiscard]] std::string dataset_reopen(const std::string& pdf);

// The "status", "code" (and "message") members of a call that did not
// succeed, as tests/dataset/outcome.md section 2.1 names them.
[[nodiscard]] std::string dataset_failure(pdfkit_status status, const std::string& text);

// SplitMix64's next value of state.
[[nodiscard]] std::uint64_t dataset_splitmix(std::uint64_t& state);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_DATASET_WRITE_HPP
