// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The redact verb, catalogue functions 1.1, 5, 6, 7, 9 and 10.1, driven by a
// JSON job file: {"input", "output", "open_options", "text", "images",
// "metadata", "pages"}. Each section present is handed byte for byte to its
// catalogue function on one edit plan, then the plan is written and the output
// file is created only when the write and its self-check succeed.

#include "pdfkit/pdfkit.h"

#include "json_text.hpp"
#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

#include <array>

namespace pdfkit::cli::verbs {

namespace {

using EditCall = pdfkit_status (*)(pdfkit_edit*, const char*, pdfkit_buffer*);

struct Section {
  std::string_view key;
  std::string_view symbol;
  EditCall call;
};

// In the order the edit plan receives them; the writer applies its own fixed
// order (CLAUDE.md section 3.2) whatever this order is.
constexpr std::array<Section, 4> sections{{
    {"text", "pdfkit_redact_text", &pdfkit_redact_text},
    {"images", "pdfkit_redact_images", &pdfkit_redact_images},
    {"metadata", "pdfkit_set_metadata", &pdfkit_set_metadata},
    {"pages", "pdfkit_select_pages", &pdfkit_select_pages},
}};

constexpr std::string_view job_shape =
    R"(a JSON object {"input": "<file.pdf>", "output": "<file.pdf>"} with at least one of the sections "text", "images", "metadata", "pages" (docs/ABI.md))";

int job_error(const std::string& failed) {
  return print_usage_error("redact", failed, job_shape,
                           "correct the job file; pdfkit redact --help shows an example");
}

}  // namespace

const Arguments::Spec& redact_spec() {
  static const Arguments::Spec spec{
      "redact", 1, 1, {}, {}, "redact <job.json>", "redact job.json",
  };
  return spec;
}

int run_redact(std::span<char* const> args) {
  const std::optional<Arguments> parsed = Arguments::parse(args, redact_spec());
  if (!parsed) {
    return exit_usage;
  }
  const std::optional<std::vector<std::uint8_t>> job_bytes =
      read_input_file("redact", parsed->positionals().front());
  if (!job_bytes) {
    return exit_usage;
  }
  const std::string job_text(job_bytes->begin(), job_bytes->end());
  std::size_t error_offset = 0;
  const std::optional<JsonText> job = JsonText::parse(job_text, error_offset);
  if (!job || job->root().kind != JsonText::Kind::object) {
    return job_error("the job file is not a JSON object (the fault is at byte " +
                     std::to_string(error_offset) + ")");
  }
  const JsonText::Node* input = job->member(job->root(), "input");
  const JsonText::Node* output = job->member(job->root(), "output");
  if (input == nullptr || input->kind != JsonText::Kind::string || output == nullptr ||
      output->kind != JsonText::Kind::string) {
    return job_error(R"(the job file has no "input" or no "output" path)");
  }
  std::vector<std::string_view> needed;
  for (const Section& section : sections) {
    if (job->member(job->root(), section.key) != nullptr) {
      needed.push_back(section.symbol);
    }
  }
  if (needed.empty()) {
    return job_error("the job file has none of the sections text, images, metadata, pages");
  }
  // The sections whose functions this build lacks are refused before the
  // input is read, naming the section, so a job is never half applied.
  for (const Section& section : sections) {
    if ((section.key == "images" || section.key == "metadata") &&
        job->member(job->root(), section.key) != nullptr && !is_implemented(section.symbol)) {
      return print_usage_error(
          "redact",
          "the job file has an \"" + std::string(section.key) +
              "\" section, and this build lacks " + std::string(section.symbol),
          job_shape,
          "remove the section from the job or use a build that does (pdfkit --version lists "
          "what this build implements)");
    }
  }
  const std::optional<std::vector<std::uint8_t>> bytes = read_input_file("redact", input->text);
  if (!bytes) {
    return exit_usage;
  }
  for (const std::string_view symbol : needed) {
    if (const std::optional<int> missing = require_implemented({symbol})) {
      return *missing;
    }
  }
  if (const std::optional<int> missing = require_implemented({"pdfkit_write", "pdfkit_open"})) {
    return *missing;
  }

  const JsonText::Node* open_options = job->member(job->root(), "open_options");
  const std::string open_json = open_options ? std::string(open_options->raw) : std::string();
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  pdfkit_status status = pdfkit_open(bytes->data(), bytes->size(),
                                     open_options ? open_json.c_str() : nullptr, &doc, &out);
  if (status != PDFKIT_OK) {
    return print_result(status, out);
  }
  pdfkit_buffer_free(&out);
  pdfkit_edit* edit = nullptr;
  status = pdfkit_edit_new(doc, &edit, &out);
  for (const Section& section : sections) {
    const JsonText::Node* node = job->member(job->root(), section.key);
    if (status != PDFKIT_OK || node == nullptr) {
      continue;
    }
    pdfkit_buffer_free(&out);
    const std::string section_json(node->raw);
    status = section.call(edit, section_json.c_str(), &out);
  }
  pdfkit_buffer pdf{nullptr, 0, nullptr};
  if (status == PDFKIT_OK) {
    pdfkit_buffer_free(&out);
    status = pdfkit_write(edit, nullptr, &pdf, &out);
  }
  int code = exit_ok;
  if (status == PDFKIT_OK && !write_output_file("redact", output->text, pdf.data, pdf.size)) {
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
