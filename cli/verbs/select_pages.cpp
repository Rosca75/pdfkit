// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The select-pages verb, catalogue functions 1.1, 9 and 10.1: opens the PDF,
// keeps the pages given with --pages in that order, writes a fresh file to
// --out, and prints the write report. The output file is written only when the
// write succeeds, so a refused write leaves no partial file behind.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

namespace pdfkit::cli::verbs {

const Arguments::Spec& select_pages_spec() {
  static const Arguments::Spec spec{
      "select-pages",
      1,
      1,
      {"pages", "out", "open-options"},
      {"pages", "out"},
      "select-pages <file.pdf> --pages <json> --out <output.pdf> [--open-options <json>]",
      R"(select-pages report.pdf --pages "{\"pages\":[1,3]}" --out excerpt.pdf)",
  };
  return spec;
}

int run_select_pages(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, select_pages_spec());
  if (!parsed) {
    return exit_usage;
  }
  const std::optional<std::vector<std::uint8_t>> bytes =
      read_input_file("select-pages", parsed->positionals().front());
  if (!bytes) {
    return exit_usage;
  }
  if (const std::optional<int> missing =
          require_implemented({"pdfkit_select_pages", "pdfkit_write", "pdfkit_open"})) {
    return *missing;
  }
  const std::optional<std::string> open_options = parsed->option("open-options");
  const std::string pages = parsed->option("pages").value_or("");
  const std::string output = parsed->option("out").value_or("");

  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  pdfkit_status status = pdfkit_open(bytes->data(), bytes->size(),
                                     open_options ? open_options->c_str() : nullptr, &doc, &out);
  if (status != PDFKIT_OK) {
    return print_result(status, out);
  }
  pdfkit_buffer_free(&out);
  pdfkit_edit* edit = nullptr;
  status = pdfkit_edit_new(doc, &edit, &out);
  if (status == PDFKIT_OK) {
    pdfkit_buffer_free(&out);
    status = pdfkit_select_pages(edit, pages.c_str(), &out);
  }
  pdfkit_buffer pdf{nullptr, 0, nullptr};
  if (status == PDFKIT_OK) {
    pdfkit_buffer_free(&out);
    status = pdfkit_write(edit, nullptr, &pdf, &out);
  }
  int code = exit_ok;
  if (status == PDFKIT_OK && !write_output_file("select-pages", output, pdf.data, pdf.size)) {
    pdfkit_buffer_free(&out);
    code = exit_usage;
  } else {
    code = print_result(status, out);
  }
  pdfkit_buffer_free(&pdf);
  pdfkit_edit_free(edit);
  pdfkit_close(doc);
  return code;
}

}  // namespace pdfkit::cli::verbs
