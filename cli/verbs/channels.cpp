// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The channels verb, catalogue function 8: opens the PDF and prints, per row of the
// channel table, whether that hidden channel is present and its count.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

namespace pdfkit::cli::verbs {

const Arguments::Spec& channels_spec() {
  static const Arguments::Spec spec{
      "channels",
      1,
      1,
      {"options", "open-options"},
      {},
      "channels <file.pdf> [--options <json>] [--open-options <json>]",
      "channels report.pdf",
  };
  return spec;
}

int run_channels(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, channels_spec());
  if (!parsed) {
    return exit_usage;
  }
  return run_document_verb(*parsed, "channels", {"pdfkit_channels"}, &pdfkit_channels);
}

}  // namespace pdfkit::cli::verbs
