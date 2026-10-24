// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// What the verbs share: reading the file the user named into bytes (the CLI is
// the one place a path is read, CLAUDE.md section 9), writing an output file,
// asking the library which catalogue functions this build implements, and
// running a verb that opens a document and calls one function on it.

#ifndef PDFKIT_CLI_SUPPORT_HPP
#define PDFKIT_CLI_SUPPORT_HPP

#include "pdfkit/pdfkit.h"

#include "arguments.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::cli {

// The bytes of path, or no value after printing a usage error naming verb.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_input_file(std::string_view verb,
                                                                       const std::string& path);

// Writes bytes to path; prints an error and returns false when it cannot.
[[nodiscard]] bool write_output_file(std::string_view verb, const std::string& path,
                                     const std::uint8_t* data, std::size_t size);

// When a symbol of symbols is not implemented by the loaded library, prints
// the error object the library reports for the first such symbol and returns
// its exit code; no value when every symbol is implemented. The verb's own
// function comes first, so the error names it.
[[nodiscard]] std::optional<int> require_implemented(
    std::initializer_list<std::string_view> symbols);

using DocumentCall = pdfkit_status (*)(pdfkit_document*, const char*, pdfkit_buffer*);

// The shape of inspect, text, images, metadata and channels: read the file,
// check that the functions in needs and pdfkit_open are implemented, open,
// call, print, close.
int run_document_verb(const Arguments& args, std::string_view verb,
                      std::initializer_list<std::string_view> needs, DocumentCall call);

}  // namespace pdfkit::cli

#endif  // PDFKIT_CLI_SUPPORT_HPP
