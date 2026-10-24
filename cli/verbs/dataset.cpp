// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dataset verb: runs every catalogue function the dataset manifest
// expects over each file and writes one JSON outcome per file
// (tests/dataset/outcome.md owns the outcome's shape). It takes either
// --manifest <tsv> --out <folder>, or --file <path> --id <id> for one file.
// This build checks the command line and the catalogue functions the outcome
// needs, and reports the first one missing.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

namespace pdfkit::cli::verbs {

const Arguments::Spec& dataset_spec() {
  static const Arguments::Spec spec{
      "dataset",
      0,
      0,
      {"manifest", "out", "file", "id"},
      {},
      "dataset --manifest <manifest.tsv> --out <folder> | dataset --file <file.pdf> --id <id>",
      "dataset --manifest dataset/manifest.tsv --out dataset/reports/run-1",
  };
  return spec;
}

int run_dataset(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, dataset_spec());
  if (!parsed) {
    return exit_usage;
  }
  const bool manifest_run = parsed->option("manifest") && parsed->option("out");
  const bool file_run = parsed->option("file") && parsed->option("id");
  if (manifest_run == file_run) {
    return print_usage_error(
        "dataset", "the options name neither a manifest run nor a single-file run, or both",
        "pdfkit " + std::string(dataset_spec().usage),
        "for example: pdfkit " + std::string(dataset_spec().example));
  }
  if (const std::optional<std::string> file = parsed->option("file"); file_run && file) {
    if (!read_input_file("dataset", *file)) {
      return exit_usage;
    }
  }
  if (const std::optional<int> missing = require_implemented({"pdfkit_open", "pdfkit_inspect"})) {
    return *missing;
  }
  return print_error(
      "not_implemented",
      "the dataset verb's outcome writer is not part of this build of pdfkit.exe",
      "a pdfkit.exe whose dataset verb writes the outcome of tests/dataset/outcome.md",
      "use a pdfkit build that delivers the dataset verb (build-3.md)", exit_internal);
}

}  // namespace pdfkit::cli::verbs
