// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The find verb, catalogue function 11: opens the PDF and prints, for each
// value given with --values (the values JSON of pdfkit_find, its pages and
// context included), every occurrence with its glyph ranges, rectangles,
// hidden classes and context. It writes no file.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::cli::verbs {

const Arguments::Spec& find_spec() {
  static const Arguments::Spec spec{
      "find",
      1,
      1,
      {"values", "open-options"},
      {"values"},
      "find <file.pdf> --values <json> [--open-options <json>]",
      R"(find report.pdf --values "{\"values\":[{\"text\":\"EU\"}],\"context\":3}")",
  };
  return spec;
}

int run_find(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, find_spec());
  if (!parsed) {
    return exit_usage;
  }
  const std::optional<std::vector<std::uint8_t>> bytes =
      read_input_file("find", parsed->positionals().front());
  if (!bytes) {
    return exit_usage;
  }
  if (const std::optional<int> missing = require_implemented({"pdfkit_find", "pdfkit_open"})) {
    return *missing;
  }
  const std::string values = parsed->option("values").value_or("");
  const std::optional<std::string> open_options = parsed->option("open-options");
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  const pdfkit_status opened = pdfkit_open(
      bytes->data(), bytes->size(), open_options ? open_options->c_str() : nullptr, &doc, &out);
  if (opened != PDFKIT_OK) {
    return print_result(opened, out);
  }
  pdfkit_buffer_free(&out);
  const pdfkit_status status = pdfkit_find(doc, values.c_str(), &out);
  const int code = print_result(status, out);
  pdfkit_close(doc);
  return code;
}

}  // namespace pdfkit::cli::verbs
