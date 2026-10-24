// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The CLI's output: the status-to-exit-code mapping, JSON string quoting for
// the errors the CLI writes itself, and the printing of library buffers.

#include "output.hpp"

#include <cstdio>

namespace pdfkit::cli {

int exit_code_of(pdfkit_status status) noexcept {
  switch (status) {
    case PDFKIT_OK:
      return exit_ok;
    case PDFKIT_E_INVALID_ARGUMENT:
      return exit_usage;
    case PDFKIT_E_REFUSED:
      return exit_refused;
    case PDFKIT_E_SELF_CHECK:
      return exit_self_check;
    case PDFKIT_E_NOT_IMPLEMENTED:
    case PDFKIT_E_INTERNAL:
      return exit_internal;
  }
  return exit_internal;
}

std::string json_quote(std::string_view text) {
  static constexpr std::string_view hex = "0123456789abcdef";
  std::string out = "\"";
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if (byte < 0x20U) {
      out += "\\u00";
      out.push_back(hex[byte >> 4U]);
      out.push_back(hex[byte & 0x0FU]);
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

void print_json(std::string_view json) {
  std::fwrite(json.data(), 1, json.size(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

int print_error(std::string_view code, std::string_view failed, std::string_view expected,
                std::string_view fix, int exit_code) {
  print_json("{\"code\":" + json_quote(code) + ",\"failed\":" + json_quote(failed) +
             ",\"expected\":" + json_quote(expected) + ",\"fix\":" + json_quote(fix) + "}");
  return exit_code;
}

int print_usage_error(std::string_view verb, std::string_view failed, std::string_view expected,
                      std::string_view fix) {
  const std::string command = verb.empty() ? "pdfkit" : "pdfkit " + std::string(verb);
  return print_error("usage", command + ": " + std::string(failed), expected, fix, exit_usage);
}

int print_result(pdfkit_status status, pdfkit_buffer& out) {
  if (out.data != nullptr) {
    print_json({reinterpret_cast<const char*>(out.data), out.size});
  } else if (status != PDFKIT_OK) {
    print_error("internal", "the library returned a failure status without an error object",
                "an error object with every failure status",
                "this is a defect in pdfkit; report the command that caused it", exit_internal);
  }
  pdfkit_buffer_free(&out);
  return exit_code_of(status);
}

}  // namespace pdfkit::cli
