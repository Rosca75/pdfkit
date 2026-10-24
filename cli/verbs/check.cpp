// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The check verb, catalogue function 10.2: searches any PDF for surviving
// occurrences of the strings given with --strings, under each string's
// folding rules, and prints every occurrence by page and surface.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

namespace pdfkit::cli::verbs {

const Arguments::Spec& check_spec() {
  static const Arguments::Spec spec{
      "check",
      1,
      1,
      {"strings"},
      {"strings"},
      "check <file.pdf> --strings <json>",
      R"(check redacted.pdf --strings "{\"strings\":[{\"text\":\"Jane Doe\"}]}")",
  };
  return spec;
}

int run_check(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, check_spec());
  if (!parsed) {
    return exit_usage;
  }
  const std::optional<std::vector<std::uint8_t>> bytes =
      read_input_file("check", parsed->positionals().front());
  if (!bytes) {
    return exit_usage;
  }
  if (const std::optional<int> missing = require_implemented({"pdfkit_check"})) {
    return *missing;
  }
  const std::string strings = parsed->option("strings").value_or("");
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_check(bytes->data(), bytes->size(), strings.c_str(), &out);
  return print_result(status, out);
}

}  // namespace pdfkit::cli::verbs
