// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The text verb, catalogue function 2: opens the PDF and prints the text of its
// pages in reading order, and with --glyphs the glyph records behind it.
// --pages takes page numbers and ranges ("1-3,7"); the two flags are written
// into the options JSON, which --options gives whole instead.

#include "pdfkit/pdfkit.h"

#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::cli::verbs {

namespace {

// The JSON array of the pages "1-3,7" names, or no value when it does not
// read as page numbers and ranges.
std::optional<std::string> pages_array(std::string_view text) {
  std::string out = "[";
  bool first = true;
  while (!text.empty()) {
    const std::size_t comma = text.find(',');
    const std::string_view part = text.substr(0, comma);
    text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    const std::size_t dash = part.find('-');
    const std::string_view low_text = part.substr(0, dash);
    const std::string_view high_text =
        dash == std::string_view::npos ? low_text : part.substr(dash + 1);
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    const auto [low_end, low_error] =
        std::from_chars(low_text.data(), low_text.data() + low_text.size(), low);
    const auto [high_end, high_error] =
        std::from_chars(high_text.data(), high_text.data() + high_text.size(), high);
    if (low_error != std::errc() || high_error != std::errc() ||
        low_end != low_text.data() + low_text.size() ||
        high_end != high_text.data() + high_text.size() || low == 0 || high < low ||
        high - low > 100000) {
      return std::nullopt;
    }
    for (std::uint32_t page = low; page <= high; ++page) {
      out += (first ? "" : ",") + std::to_string(page);
      first = false;
    }
  }
  if (first) {
    return std::nullopt;
  }
  return out + "]";
}

}  // namespace

const Arguments::Spec& text_spec() {
  static const Arguments::Spec spec{
      "text",
      1,
      1,
      {"options", "open-options", "pages"},
      {},
      "text <file.pdf> [--pages <1-3,7>] [--glyphs] [--options <json>] [--open-options <json>]",
      "text report.pdf --pages 1-2 --glyphs",
  };
  return spec;
}

int run_text(std::span<char* const> args) {
  // --glyphs is a flag; Arguments reads --name value options only.
  std::vector<char*> rest;
  bool glyphs = false;
  for (char* const arg : args) {
    if (std::string_view(arg) == "--glyphs") {
      glyphs = true;
    } else {
      rest.push_back(arg);
    }
  }
  std::optional<Arguments> parsed = Arguments::parse(rest, text_spec());
  if (!parsed) {
    return exit_usage;
  }
  const std::optional<std::string> pages = parsed->option("pages");
  if (!pages && !glyphs) {
    return run_document_verb(*parsed, "text", {"pdfkit_text"}, &pdfkit_text);
  }
  if (parsed->option("options")) {
    return print_usage_error("text", "--options was given together with --pages or --glyphs",
                             "either --options with the whole options JSON, or the flags",
                             "write the pages and glyphs into --options, or drop --options");
  }
  std::string options = "{";
  if (pages) {
    const std::optional<std::string> list = pages_array(*pages);
    if (!list) {
      return print_usage_error("text", "--pages " + *pages + " does not read as page numbers",
                               "page numbers and ranges from 1, such as 1-3,7",
                               "for example: pdfkit text report.pdf --pages 1-3,7");
    }
    options += R"("pages": )" + *list;
  }
  if (glyphs) {
    options += std::string(pages ? ", " : "") + R"("glyphs": true)";
  }
  options += '}';
  std::vector<std::string> storage{"--options", options};
  rest.push_back(storage[0].data());
  rest.push_back(storage[1].data());
  parsed = Arguments::parse(rest, text_spec());
  if (!parsed) {
    return exit_usage;
  }
  return run_document_verb(*parsed, "text", {"pdfkit_text"}, &pdfkit_text);
}

}  // namespace pdfkit::cli::verbs
