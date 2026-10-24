// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The catalogue registry: one row per function of CLAUDE.md section 2, with its
// number, its C symbol, the CLI verb that exposes it, the build file that
// delivers it and whether this build implements it. The not-implemented error
// and pdfkit_version read it, and build/check_catalogue.py parses
// catalogue.cpp to hold it to the charter, the header and the CLI verbs.

#ifndef PDFKIT_API_CATALOGUE_HPP
#define PDFKIT_API_CATALOGUE_HPP

#include "platform/error.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace pdfkit::api {

class Catalogue {
 public:
  enum class Status : std::uint8_t { not_implemented, implemented };

  struct Row {
    std::string_view number;
    std::string_view function;
    std::string_view symbol;
    std::string_view verb;
    std::string_view build_file;
    Status status;
  };

  // The rows in the order of CLAUDE.md section 2.
  [[nodiscard]] static std::span<const Row> rows() noexcept;

  // The row of a symbol; an internal error when the symbol has none.
  [[nodiscard]] static const Row& find(std::string_view symbol);

  // The error a function returns while its build file has not landed.
  [[nodiscard]] static platform::Error not_implemented_error(const Row& row);

  // Throws not_implemented_error for symbol.
  [[noreturn]] static void throw_not_implemented(std::string_view symbol);
};

}  // namespace pdfkit::api

#endif  // PDFKIT_API_CATALOGUE_HPP
