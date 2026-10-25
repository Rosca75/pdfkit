// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The select-pages verb, catalogue functions 1.1, 9 and 10.1: opens the PDF,
// keeps the pages given with --pages in that order, writes a fresh file to
// --out, and prints the write report. --pages is a list of page numbers and
// ascending ranges separated by commas ("1,3-5" keeps pages 1, 3, 4 and 5);
// anything else is a usage error before the file is read. The output file is
// written only when the write succeeds, so a refused write leaves no partial
// file behind.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace pdfkit::cli::verbs {

namespace {

// The most pages a range may expand to, so "1-999999999" stays a usage
// error rather than a gigabyte of JSON.
constexpr std::uint64_t max_pages = 1000000;

std::optional<std::uint64_t> number_of(std::string_view text) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value == 0) {
    return std::nullopt;
  }
  return value;
}

// {"pages": [...]} from "1,3-5", or no value when the text is not such a list.
std::optional<std::string> pages_json(std::string_view text) {
  std::string json = "{\"pages\": [";
  std::uint64_t count = 0;
  std::size_t start = 0;
  while (true) {
    const std::size_t comma = text.find(',', start);
    const std::string_view item =
        text.substr(start, comma == std::string_view::npos ? text.size() - start : comma - start);
    const std::size_t dash = item.find('-');
    const std::optional<std::uint64_t> first = number_of(item.substr(0, dash));
    const std::optional<std::uint64_t> last =
        dash == std::string_view::npos ? first : number_of(item.substr(dash + 1));
    if (!first || !last || *last < *first || *last - *first >= max_pages - count) {
      return std::nullopt;
    }
    for (std::uint64_t page = *first; page <= *last; ++page) {
      json += count++ == 0 ? "" : ", ";
      json += std::to_string(page);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return json + "]}";
}

}  // namespace

const Arguments::Spec& select_pages_spec() {
  static const Arguments::Spec spec{
      "select-pages",
      1,
      1,
      {"pages", "out", "open-options"},
      {"pages", "out"},
      "select-pages <file.pdf> --pages <list> --out <output.pdf> [--open-options <json>]",
      "select-pages report.pdf --pages 1,3-5 --out excerpt.pdf",
  };
  return spec;
}

int run_select_pages(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, select_pages_spec());
  if (!parsed) {
    return exit_usage;
  }
  const std::string list = parsed->option("pages").value_or("");
  const std::optional<std::string> pages = pages_json(list);
  if (!pages) {
    return print_usage_error(
        "select-pages", "--pages " + list + " is not a list of pages",
        "page numbers from 1 and ascending ranges separated by commas, such as 1,3-5",
        "for example: pdfkit " + std::string(select_pages_spec().example));
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
    status = pdfkit_select_pages(edit, pages->c_str(), &out);
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
