// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// How the CLI answers: JSON on standard output and a stable exit code
// (docs/ABI.md, the CLI chapter). A result or an error the library returned is
// printed byte for byte; an error the CLI itself finds (a usage error, a file
// it cannot read or write) is printed in the same four-field shape, so a
// caller parses one shape whatever went wrong.

#ifndef PDFKIT_CLI_OUTPUT_HPP
#define PDFKIT_CLI_OUTPUT_HPP

#include "pdfkit/pdfkit.h"

#include <string>
#include <string_view>

namespace pdfkit::cli {

// The exit codes of CLAUDE.md section 2.
inline constexpr int exit_ok = 0;
inline constexpr int exit_usage = 1;
inline constexpr int exit_refused = 2;
inline constexpr int exit_self_check = 3;
inline constexpr int exit_internal = 4;

// The exit code a status maps to; PDFKIT_E_NOT_IMPLEMENTED maps to 4, because
// a missing function is a defect from the caller's point of view.
[[nodiscard]] int exit_code_of(pdfkit_status status) noexcept;

// A JSON string literal for text, escaped per RFC 8259.
[[nodiscard]] std::string json_quote(std::string_view text);

// The folding member the dataset jobs send with every value, search and
// occurrence: every flag on, as a caller that folds names it, because the
// C ABI folds nothing it is not asked to.
inline constexpr std::string_view every_folding =
    R"("folding": {"case": true, "diacritics": true, "whitespace": true, "hyphenation": true})";

// Prints one JSON document and a newline to standard output.
void print_json(std::string_view json);

// Prints {"code", "failed", "expected", "fix"} and returns exit_code.
int print_error(std::string_view code, std::string_view failed, std::string_view expected,
                std::string_view fix, int exit_code);

// A usage error of verb (exit code 1).
int print_usage_error(std::string_view verb, std::string_view failed, std::string_view expected,
                      std::string_view fix);

// Prints what a call left in out (its result or its error object), frees out,
// and returns the exit code of status.
int print_result(pdfkit_status status, pdfkit_buffer& out);

}  // namespace pdfkit::cli

#endif  // PDFKIT_CLI_OUTPUT_HPP
