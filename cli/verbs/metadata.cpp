// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The metadata verb, catalogue function 4: opens the PDF and prints its Info
// dictionary and XMP packet side by side.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

namespace pdfkit::cli::verbs {

const Arguments::Spec& metadata_spec() {
  static const Arguments::Spec spec{
      "metadata",
      1,
      1,
      {"options", "open-options"},
      {},
      "metadata <file.pdf> [--options <json>] [--open-options <json>]",
      "metadata report.pdf",
  };
  return spec;
}

int run_metadata(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, metadata_spec());
  if (!parsed) {
    return exit_usage;
  }
  return run_document_verb(*parsed, "metadata", {"pdfkit_metadata"}, &pdfkit_metadata);
}

}  // namespace pdfkit::cli::verbs
