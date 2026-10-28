// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The catalogue table. A row's status flips to implemented in the change that
// lands its function; build/check_catalogue.py reads the rows below, one per
// line, so their layout stays one brace-enclosed initialiser per row.

#include "api/catalogue.hpp"

#include <array>
#include <string>

namespace pdfkit::api {

namespace {

using enum Catalogue::Status;

constexpr std::array<Catalogue::Row, 14> catalogue_rows{{
    {"1.1", "Open", "pdfkit_open", "inspect", "build-3.md", implemented},
    {"1.2", "Inspect", "pdfkit_inspect", "inspect", "build-3.md", implemented},
    {"2", "Extract text", "pdfkit_text", "text", "build-5.md", implemented},
    {"3.1", "List images", "pdfkit_images", "images", "build-6.md", implemented},
    {"3.2", "Image pixels", "pdfkit_image_data", "images", "build-6.md", implemented},
    {"4", "Extract metadata", "pdfkit_metadata", "metadata", "build-10.md", implemented},
    {"5", "Redact text", "pdfkit_redact_text", "redact", "build-8.md", implemented},
    {"6", "Redact images", "pdfkit_redact_images", "redact", "build-9.md", implemented},
    {"7", "Rewrite metadata", "pdfkit_set_metadata", "redact", "build-10.md", implemented},
    {"8", "Strip hidden channels", "pdfkit_channels", "channels", "build-7.md", implemented},
    {"9", "Select pages", "pdfkit_select_pages", "select-pages", "build-7.md", implemented},
    {"10.1", "Write", "pdfkit_write", "redact", "build-7.md", implemented},
    {"10.2", "Check", "pdfkit_check", "check", "build-8.md", implemented},
    {"11", "Find", "pdfkit_find", "find", "build-12.md", implemented},
}};

}  // namespace

std::span<const Catalogue::Row> Catalogue::rows() noexcept {
  return catalogue_rows;
}

const Catalogue::Row& Catalogue::find(std::string_view symbol) {
  for (const Row& row : catalogue_rows) {
    if (row.symbol == symbol) {
      return row;
    }
  }
  throw platform::Error(platform::ErrorCode::internal,
                        std::string(symbol) + " has no row in the catalogue registry",
                        "every exported catalogue symbol listed in src/api/catalogue.cpp",
                        "this is a defect in pdfkit; report it");
}

platform::Error Catalogue::not_implemented_error(const Row& row) {
  const std::string symbol(row.symbol);
  return {platform::ErrorCode::not_implemented,
          symbol + " (catalogue function " + std::string(row.number) + ", " +
              std::string(row.function) + ") is not implemented in this build of pdfkit",
          "a pdfkit build that implements " + symbol + ", which " + std::string(row.build_file) +
              " delivers",
          "use a pdfkit build whose pdfkit_version output lists " + symbol +
              " with \"implemented\": true"};
}

void Catalogue::throw_not_implemented(std::string_view symbol) {
  throw not_implemented_error(find(symbol));
}

}  // namespace pdfkit::api
