// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The inspect verb, catalogue functions 1.1 and 1.2: opens the PDF and prints its
// version, page count, page boxes and rotation, encryption, repairs and warnings.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

namespace pdfkit::cli::verbs {

const Arguments::Spec& inspect_spec() {
  static const Arguments::Spec spec{
      "inspect",
      1,
      1,
      {"options", "open-options"},
      {},
      "inspect <file.pdf> [--options <json>] [--open-options <json>]",
      "inspect report.pdf",
  };
  return spec;
}

int run_inspect(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, inspect_spec());
  if (!parsed) {
    return exit_usage;
  }
  return run_document_verb(*parsed, "inspect", {"pdfkit_inspect"}, &pdfkit_inspect);
}

}  // namespace pdfkit::cli::verbs
