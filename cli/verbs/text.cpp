// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The text verb, catalogue function 2: opens the PDF and prints the text of its
// pages in reading order with the glyph records behind it.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

namespace pdfkit::cli::verbs {

const Arguments::Spec& text_spec() {
  static const Arguments::Spec spec{
      "text",
      1,
      1,
      {"options", "open-options"},
      {},
      "text <file.pdf> [--options <json>] [--open-options <json>]",
      "text report.pdf",
  };
  return spec;
}

int run_text(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, text_spec());
  if (!parsed) {
    return exit_usage;
  }
  return run_document_verb(*parsed, "text", {"pdfkit_text"}, &pdfkit_text);
}

}  // namespace pdfkit::cli::verbs
