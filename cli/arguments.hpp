// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Arguments holds one verb's command line after the verb: its positional
// arguments and its --name value options, checked against the verb's spec, so
// a wrong command line is a usage error naming the verb, what was expected and
// an example, before any file is read.

#ifndef PDFKIT_CLI_ARGUMENTS_HPP
#define PDFKIT_CLI_ARGUMENTS_HPP

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::cli {

class Arguments {
 public:
  struct Spec {
    std::string_view verb;
    std::size_t min_positionals;
    std::size_t max_positionals;
    std::vector<std::string_view> options;
    std::vector<std::string_view> required_options;
    std::string_view usage;
    std::string_view example;
  };

  // The parsed arguments, or no value after printing the usage error.
  [[nodiscard]] static std::optional<Arguments> parse(std::span<char* const> args,
                                                      const Spec& spec);

  [[nodiscard]] const std::vector<std::string>& positionals() const { return positionals_; }
  // The value of --name, or no value when it was not given.
  [[nodiscard]] std::optional<std::string> option(std::string_view name) const;

 private:
  std::vector<std::string> positionals_;
  std::vector<std::pair<std::string, std::string>> options_;
};

}  // namespace pdfkit::cli

#endif  // PDFKIT_CLI_ARGUMENTS_HPP
