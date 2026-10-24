// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The shared steps of the verbs. Files are read and written with the standard
// library's narrow-path streams; the executable's manifest sets the process
// code page to UTF-8 (cli/pdfkit.manifest), so a path with accents or any
// other script reaches the file system intact.

#include "support.hpp"

#include "json_text.hpp"
#include "output.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>

namespace pdfkit::cli {

std::optional<std::vector<std::uint8_t>> read_input_file(std::string_view verb,
                                                         const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    print_usage_error(verb, "cannot read " + path + ": " + std::strerror(errno),
                      "the path of a readable file",
                      "check the path and that the file exists and is not open elsewhere");
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
  if (file.bad()) {
    print_usage_error(verb, "reading " + path + " failed part way", "a readable file",
                      "copy the file to a local disk and try again");
    return std::nullopt;
  }
  return bytes;
}

bool write_output_file(std::string_view verb, const std::string& path, const std::uint8_t* data,
                       std::size_t size) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (file) {
    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  }
  if (!file) {
    print_error(
        "output_failed",
        "pdfkit " + std::string(verb) + ": cannot write " + path + ": " + std::strerror(errno),
        "a writable path for the output file",
        "check that the folder exists, that you may write to it and that the file is not "
        "open elsewhere",
        exit_usage);
    return false;
  }
  return true;
}

std::optional<int> require_implemented(std::initializer_list<std::string_view> symbols) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_version(&out);
  if (status != PDFKIT_OK) {
    return print_result(status, out);
  }
  const std::string text(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  std::size_t error_offset = 0;
  const std::optional<JsonText> version = JsonText::parse(text, error_offset);
  const JsonText::Node* catalogue = nullptr;
  if (version) {
    catalogue = version->member(version->root(), "catalogue");
  }
  if (!version || catalogue == nullptr || catalogue->kind != JsonText::Kind::array) {
    return print_error("internal", "pdfkit_version returned JSON without a catalogue array",
                       "the version object of docs/ABI.md",
                       "pdfkit.exe and pdfkit.dll come from different builds; use the pair "
                       "shipped together",
                       exit_internal);
  }
  const JsonText& document = *version;
  for (const std::string_view symbol : symbols) {
    for (const std::size_t index : catalogue->items) {
      const JsonText::Node& row = document.node(index);
      const JsonText::Node* name = document.member(row, "symbol");
      const JsonText::Node* implemented = document.member(row, "implemented");
      if (name == nullptr || name->text != symbol || implemented == nullptr) {
        continue;
      }
      if (implemented->text == "true") {
        break;
      }
      const JsonText::Node* error = document.member(row, "error");
      if (error != nullptr) {
        print_json(error->raw);
        return exit_internal;
      }
      return print_error(
          "not_implemented", std::string(symbol) + " is not implemented in this build of pdfkit",
          "a pdfkit build that implements " + std::string(symbol),
          "use a pdfkit build whose pdfkit_version output lists it as implemented", exit_internal);
    }
  }
  return std::nullopt;
}

int run_document_verb(const Arguments& args, std::string_view verb,
                      std::initializer_list<std::string_view> needs, DocumentCall call) {
  const std::optional<std::vector<std::uint8_t>> bytes =
      read_input_file(verb, args.positionals().front());
  if (!bytes) {
    return exit_usage;
  }
  if (const std::optional<int> missing = require_implemented(needs)) {
    return *missing;
  }
  if (const std::optional<int> missing = require_implemented({"pdfkit_open"})) {
    return *missing;
  }
  const std::optional<std::string> options = args.option("options");
  const std::optional<std::string> open_options = args.option("open-options");
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  const pdfkit_status opened = pdfkit_open(
      bytes->data(), bytes->size(), open_options ? open_options->c_str() : nullptr, &doc, &out);
  if (opened != PDFKIT_OK) {
    return print_result(opened, out);
  }
  pdfkit_buffer_free(&out);
  const pdfkit_status status = call(doc, options ? options->c_str() : nullptr, &out);
  const int code = print_result(status, out);
  pdfkit_close(doc);
  return code;
}

}  // namespace pdfkit::cli
