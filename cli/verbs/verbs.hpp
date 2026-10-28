// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The verbs of CLAUDE.md section 2, one function per file of this folder. Each
// takes the arguments after the verb, prints JSON on standard output and
// returns the exit code; main.cpp holds the table that dispatches to them.

#ifndef PDFKIT_CLI_VERBS_VERBS_HPP
#define PDFKIT_CLI_VERBS_VERBS_HPP

#include "arguments.hpp"

#include <span>

namespace pdfkit::cli::verbs {

// The argument spec of each verb, which main.cpp also prints as its help.
[[nodiscard]] const Arguments::Spec& inspect_spec();
[[nodiscard]] const Arguments::Spec& text_spec();
[[nodiscard]] const Arguments::Spec& images_spec();
[[nodiscard]] const Arguments::Spec& metadata_spec();
[[nodiscard]] const Arguments::Spec& redact_spec();
[[nodiscard]] const Arguments::Spec& select_pages_spec();
[[nodiscard]] const Arguments::Spec& check_spec();
[[nodiscard]] const Arguments::Spec& channels_spec();
[[nodiscard]] const Arguments::Spec& find_spec();
[[nodiscard]] const Arguments::Spec& dataset_spec();

int run_inspect(std::span<char* const> args);
int run_text(std::span<char* const> args);
int run_images(std::span<char* const> args);
int run_metadata(std::span<char* const> args);
int run_redact(std::span<char* const> args);
int run_select_pages(std::span<char* const> args);
int run_check(std::span<char* const> args);
int run_channels(std::span<char* const> args);
int run_find(std::span<char* const> args);
int run_dataset(std::span<char* const> args);

}  // namespace pdfkit::cli::verbs

#endif  // PDFKIT_CLI_VERBS_VERBS_HPP
