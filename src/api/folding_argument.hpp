// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FoldingArgument reads the "folding" object a caller gives pdfkit_find,
// pdfkit_redact_text (its search and occurrence forms) and pdfkit_check
// (docs/ABI.md, the pdfkit_redact_text section): four flags, each false
// unless the caller sets it true, because pdfkit folds only what it is asked
// to (CLAUDE.md section 5); "equivalents", the caller's classes of
// characters that match one another; and "ignore", the characters read as
// absent. One reader serves the three functions, so a folding means the same
// in each.

#ifndef PDFKIT_API_FOLDING_ARGUMENT_HPP
#define PDFKIT_API_FOLDING_ARGUMENT_HPP

#include "platform/json_value.hpp"
#include "redact/match_finder.hpp"

#include <string>

namespace pdfkit::api {

class FoldingArgument {
 public:
  // The folding of value; where names the place in the caller's argument
  // ("pdfkit_find values_json value 0") and expected the argument's shape.
  // Throws platform::Error (invalid_argument) for anything but an object of
  // the keys above, a flag that is not a boolean, a class or an ignored
  // character that is not one code point, an empty class, and a character
  // named twice (in two classes, or in a class and in ignore).
  [[nodiscard]] static redact::MatchFinder::Folding read(const platform::JsonValue& value,
                                                         const std::string& where,
                                                         const std::string& expected);
};

}  // namespace pdfkit::api

#endif  // PDFKIT_API_FOLDING_ARGUMENT_HPP
