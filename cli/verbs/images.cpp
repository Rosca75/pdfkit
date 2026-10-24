// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The images verb, catalogue functions 3.1 and 3.2: opens the PDF and prints
// the list of its images with their identities and occurrences. With --out,
// the image files are written into that folder; a folder that cannot be
// written is reported by the first write. std::filesystem stays out of the
// CLI, because it alone would triple the size of pdfkit.exe.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

namespace pdfkit::cli::verbs {

const Arguments::Spec& images_spec() {
  static const Arguments::Spec spec{
      "images",
      1,
      1,
      {"out", "options", "open-options"},
      {},
      "images <file.pdf> [--out <folder>] [--options <json>] [--open-options <json>]",
      "images report.pdf --out report-images",
  };
  return spec;
}

int run_images(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, images_spec());
  if (!parsed) {
    return exit_usage;
  }
  return run_document_verb(*parsed, "images", {"pdfkit_images", "pdfkit_image_data"},
                           &pdfkit_images);
}

}  // namespace pdfkit::cli::verbs
