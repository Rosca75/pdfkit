// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The images verb, catalogue functions 3.1 and 3.2: opens the PDF and prints
// the list of its images with their identities and occurrences (the JSON of
// pdfkit_images). With --out, it writes each image into that folder as
// <identity digits>.png or .jpg, and each image's mask as
// <identity digits>.mask.png, and adds "files" to the printed JSON: one
// {"identity", "file"} per file written, and the images skipped because they
// do not decode with their code. A folder that cannot be written is reported
// by the first write. std::filesystem stays out of the CLI, because it alone
// would triple the size of pdfkit.exe.

#include "pdfkit/pdfkit.h"

#include "json_text.hpp"
#include "output.hpp"
#include "support.hpp"
#include "verbs/verbs.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::cli::verbs {

namespace {

struct Written {
  std::string identity;
  std::string file;
  // Empty for a file written; otherwise the error code that kept it out.
  std::string skipped;
};

std::string folder_path(std::string folder) {
  if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') {
    folder += '/';
  }
  return folder;
}

// Writes one image (or its mask) into folder; false when the folder cannot be
// written, which has been reported.
bool write_image(pdfkit_document* doc, const std::string& identity, bool mask,
                 const std::string& folder, std::vector<Written>& written) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status =
      pdfkit_image_data(doc, identity.c_str(), mask ? R"({"mask": true})" : nullptr, &out);
  const std::string digits = identity.size() > 4 ? identity.substr(4) : identity;
  if (status != PDFKIT_OK) {
    const std::string text(reinterpret_cast<const char*>(out.data), out.size);
    pdfkit_buffer_free(&out);
    std::size_t offset = 0;
    const std::optional<JsonText> error = JsonText::parse(text, offset);
    const JsonText::Node* code = error ? error->member(error->root(), "code") : nullptr;
    written.push_back({identity, "", code != nullptr ? code->text : std::string("error")});
    return true;
  }
  const bool jpeg = out.size >= 2 && out.data[0] == 0xFF && out.data[1] == 0xD8;
  const std::string name = digits + (mask ? ".mask.png" : jpeg ? ".jpg" : ".png");
  const bool ok = write_output_file("images", folder + name, out.data, out.size);
  pdfkit_buffer_free(&out);
  if (ok) {
    written.push_back({identity, name, ""});
  }
  return ok;
}

}  // namespace

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
  const std::optional<std::string> out_folder = parsed->option("out");
  if (!out_folder) {
    return run_document_verb(*parsed, "images", {"pdfkit_images", "pdfkit_image_data"},
                             &pdfkit_images);
  }
  const std::optional<std::vector<std::uint8_t>> bytes =
      read_input_file("images", parsed->positionals().front());
  if (!bytes) {
    return exit_usage;
  }
  if (const std::optional<int> missing =
          require_implemented({"pdfkit_images", "pdfkit_image_data", "pdfkit_open"})) {
    return *missing;
  }
  const std::optional<std::string> options = parsed->option("options");
  const std::optional<std::string> open_options = parsed->option("open-options");
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  const pdfkit_status opened = pdfkit_open(
      bytes->data(), bytes->size(), open_options ? open_options->c_str() : nullptr, &doc, &out);
  if (opened != PDFKIT_OK) {
    return print_result(opened, out);
  }
  pdfkit_buffer_free(&out);
  const pdfkit_status listed = pdfkit_images(doc, options ? options->c_str() : nullptr, &out);
  if (listed != PDFKIT_OK) {
    const int code = print_result(listed, out);
    pdfkit_close(doc);
    return code;
  }
  const std::string listing(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  std::size_t offset = 0;
  const std::optional<JsonText> parsed_listing = JsonText::parse(listing, offset);
  const JsonText::Node* images =
      parsed_listing ? parsed_listing->member(parsed_listing->root(), "images") : nullptr;
  const std::string folder = folder_path(*out_folder);
  std::vector<Written> written;
  if (parsed_listing && images != nullptr) {
    for (const std::size_t index : images->items) {
      const JsonText::Node& image = parsed_listing->node(index);
      const JsonText::Node* identity = parsed_listing->member(image, "identity");
      const JsonText::Node* mask = parsed_listing->member(image, "mask");
      if (identity == nullptr) {
        continue;
      }
      if (!write_image(doc, identity->text, false, folder, written) ||
          (mask != nullptr && mask->text != "none" &&
           !write_image(doc, identity->text, true, folder, written))) {
        pdfkit_close(doc);
        return exit_usage;
      }
    }
  }
  pdfkit_close(doc);
  std::string files = R"(,"files":[)";
  bool first = true;
  for (const Written& item : written) {
    files += first ? "" : ",";
    first = false;
    files += R"({"identity":)" + json_quote(item.identity);
    if (item.skipped.empty()) {
      files += R"(,"file":)" + json_quote(item.file) + "}";
    } else {
      files += R"(,"skipped":)" + json_quote(item.skipped) + "}";
    }
  }
  files += "]}";
  std::string printed = listing;
  if (!printed.empty() && printed.back() == '}') {
    printed.pop_back();
    printed += files;
  }
  print_json(printed);
  return exit_ok;
}

}  // namespace pdfkit::cli::verbs
