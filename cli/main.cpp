// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The entry point of the pdfkit command-line tool: the verb first, then the
// verb's arguments. It dispatches to one function per verb, prints a verb's
// help for --help, and answers an unknown verb with a JSON usage error that
// lists the verbs. The tool calls the C ABI and nothing beneath it, so every
// run exercises the surface the other languages call.

#include "pdfkit/pdfkit.h"

#include "arguments.hpp"
#include "output.hpp"
#include "verbs/verbs.hpp"

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

using pdfkit::cli::Arguments;

struct Verb {
  std::string_view name;
  std::string_view summary;
  const Arguments::Spec& (*spec)();
  int (*run)(std::span<char* const>);
};

constexpr std::array<Verb, 10> verb_table{{
    {"inspect", "the PDF's version, pages, boxes, encryption, repairs and warnings",
     &pdfkit::cli::verbs::inspect_spec, &pdfkit::cli::verbs::run_inspect},
    {"text", "the text in reading order with every glyph record", &pdfkit::cli::verbs::text_spec,
     &pdfkit::cli::verbs::run_text},
    {"images", "the images, their placements and, with --out, their files",
     &pdfkit::cli::verbs::images_spec, &pdfkit::cli::verbs::run_images},
    {"metadata", "the Info dictionary and the XMP packet side by side",
     &pdfkit::cli::verbs::metadata_spec, &pdfkit::cli::verbs::run_metadata},
    {"redact", "a redacted copy, driven by a JSON job file", &pdfkit::cli::verbs::redact_spec,
     &pdfkit::cli::verbs::run_redact},
    {"select-pages", "a copy holding only the pages given", &pdfkit::cli::verbs::select_pages_spec,
     &pdfkit::cli::verbs::run_select_pages},
    {"check", "every surviving occurrence of the strings given", &pdfkit::cli::verbs::check_spec,
     &pdfkit::cli::verbs::run_check},
    {"channels", "which hidden channels the PDF holds", &pdfkit::cli::verbs::channels_spec,
     &pdfkit::cli::verbs::run_channels},
    {"find", "every occurrence of each value, its glyph ranges and context",
     &pdfkit::cli::verbs::find_spec, &pdfkit::cli::verbs::run_find},
    {"dataset", "every catalogue function over the dataset manifest",
     &pdfkit::cli::verbs::dataset_spec, &pdfkit::cli::verbs::run_dataset},
}};

std::string verb_list() {
  std::string list;
  for (const Verb& verb : verb_table) {
    list += list.empty() ? "" : ", ";
    list += verb.name;
  }
  return list;
}

int print_overview() {
  std::printf("pdfkit: reads PDF files into JSON and writes redacted copies.\n\n");
  std::printf("usage: pdfkit <verb> [arguments], or pdfkit <verb> --help\n\n");
  for (const Verb& verb : verb_table) {
    std::printf("  %-14.*s %.*s\n", static_cast<int>(verb.name.size()), verb.name.data(),
                static_cast<int>(verb.summary.size()), verb.summary.data());
  }
  std::printf("\npdfkit --version prints the library version and its catalogue as JSON.\n");
  std::printf(
      "Exit codes: 0 success, 1 usage, 2 input refused, 3 self-check refused the "
      "output, 4 internal error or a function this build lacks.\n");
  return pdfkit::cli::exit_ok;
}

int print_verb_help(const Verb& verb) {
  const Arguments::Spec& spec = verb.spec();
  std::printf("pdfkit %.*s: %.*s\n\nusage: pdfkit %.*s\nexample: pdfkit %.*s\n",
              static_cast<int>(verb.name.size()), verb.name.data(),
              static_cast<int>(verb.summary.size()), verb.summary.data(),
              static_cast<int>(spec.usage.size()), spec.usage.data(),
              static_cast<int>(spec.example.size()), spec.example.data());
  return pdfkit::cli::exit_ok;
}

int print_version() {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_version(&out);
  return pdfkit::cli::print_result(status, out);
}

}  // namespace

int main(int argc, char** argv) {
  const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
  if (args.size() < 2) {
    return pdfkit::cli::print_usage_error(
        "", "no verb was given", "pdfkit <verb> [arguments], the verb one of " + verb_list(),
        "run pdfkit --help for the verbs and what each does");
  }
  const std::string_view first = args[1];
  if (first == "--help" || first == "-h" || first == "help") {
    return print_overview();
  }
  if (first == "--version") {
    return print_version();
  }
  for (const Verb& verb : verb_table) {
    if (verb.name != first) {
      continue;
    }
    const std::span<char* const> rest = args.subspan(2);
    if (rest.size() == 1 &&
        (std::string_view(rest[0]) == "--help" || std::string_view(rest[0]) == "-h")) {
      return print_verb_help(verb);
    }
    return verb.run(rest);
  }
  return pdfkit::cli::print_usage_error(first, "unknown verb " + std::string(first),
                                        "one of " + verb_list(),
                                        "run pdfkit --help for the verbs and what each does");
}
