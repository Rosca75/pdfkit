// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The seven decompositions, scanned over the UTF-8 text one sequence at a
// time; an ill-formed sequence is copied as it is, since the text was built
// by this layer and is well formed.

#include "fonts/ligatures.hpp"

#include "platform/utf8_sequence.hpp"

namespace pdfkit::fonts {

std::string_view Ligatures::letters(char32_t code_point) noexcept {
  switch (code_point) {
    case U'ﬀ':
      return "ff";
    case U'ﬁ':
      return "fi";
    case U'ﬂ':
      return "fl";
    case U'ﬃ':
      return "ffi";
    case U'ﬄ':
      return "ffl";
    case U'ﬅ':
      // LATIN SMALL LETTER LONG S, then t.
      return "\xC5\xBFt";
    case U'ﬆ':
      return "st";
    default:
      return {};
  }
}

std::optional<std::string> Ligatures::decompose(std::string_view text) {
  std::string out;
  bool changed = false;
  std::size_t at = 0;
  while (at < text.size()) {
    const platform::Utf8Sequence sequence = platform::next_utf8_sequence(text, at);
    const std::string_view replacement =
        sequence.valid ? letters(sequence.code_point) : std::string_view();
    if (!replacement.empty()) {
      out += replacement;
      changed = true;
    } else {
      out += text.substr(at, sequence.length);
    }
    at += sequence.length;
  }
  if (!changed) {
    return std::nullopt;
  }
  return out;
}

}  // namespace pdfkit::fonts
