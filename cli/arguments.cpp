// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The command-line grammar of a verb: positional arguments, and options
// written as --name value. An unknown option, a missing value, a repeated
// option or a wrong count of positionals is a usage error (exit code 1).

#include "arguments.hpp"

#include "output.hpp"

#include <algorithm>

namespace pdfkit::cli {

namespace {

std::string spec_expectation(const Arguments::Spec& spec) {
  return "pdfkit " + std::string(spec.usage);
}

std::string spec_fix(const Arguments::Spec& spec) {
  return "for example: pdfkit " + std::string(spec.example) + " (pdfkit " + std::string(spec.verb) +
         " --help lists the options)";
}

}  // namespace

std::optional<Arguments> Arguments::parse(std::span<char* const> args, const Spec& spec) {
  Arguments result;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg.starts_with("--")) {
      const std::string_view name = arg.substr(2);
      if (std::ranges::find(spec.options, name) == spec.options.end()) {
        print_usage_error(spec.verb, "unknown option " + std::string(arg), spec_expectation(spec),
                          spec_fix(spec));
        return std::nullopt;
      }
      if (i + 1 >= args.size()) {
        print_usage_error(spec.verb, "the option " + std::string(arg) + " has no value",
                          spec_expectation(spec), spec_fix(spec));
        return std::nullopt;
      }
      if (result.option(name).has_value()) {
        print_usage_error(spec.verb, "the option " + std::string(arg) + " is given twice",
                          spec_expectation(spec), spec_fix(spec));
        return std::nullopt;
      }
      result.options_.emplace_back(std::string(name), std::string(args[i + 1]));
      ++i;
      continue;
    }
    result.positionals_.emplace_back(arg);
  }
  if (result.positionals_.size() < spec.min_positionals ||
      result.positionals_.size() > spec.max_positionals) {
    print_usage_error(spec.verb,
                      "pdfkit " + std::string(spec.verb) + " was given " +
                          std::to_string(result.positionals_.size()) + " file arguments",
                      spec_expectation(spec), spec_fix(spec));
    return std::nullopt;
  }
  for (const std::string_view required : spec.required_options) {
    if (!result.option(required).has_value()) {
      print_usage_error(spec.verb, "the option --" + std::string(required) + " is missing",
                        spec_expectation(spec), spec_fix(spec));
      return std::nullopt;
    }
  }
  return result;
}

std::optional<std::string> Arguments::option(std::string_view name) const {
  for (const auto& [key, value] : options_) {
    if (key == name) {
      return value;
    }
  }
  return std::nullopt;
}

}  // namespace pdfkit::cli
