// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// UTF-8 well-formedness and encoding, shared by the JSON writer (which replaces
// an ill-formed sequence with U+FFFD so its output stays valid JSON) and the
// JSON reader (which refuses one). The well-formed byte sequences are those of
// the Unicode Standard, chapter 3, table 3-7, and the replacement unit is the
// "maximal subpart" of section 3.9 (recalled; tested in json_writer_test.cpp).

#ifndef PDFKIT_PLATFORM_UTF8_SEQUENCE_HPP
#define PDFKIT_PLATFORM_UTF8_SEQUENCE_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace pdfkit::platform {

// The sequence starting at text[at]: when valid, its length (1 to 4) and its
// code point; when ill-formed, the length of its maximal subpart (at least 1),
// which one U+FFFD replaces.
struct Utf8Sequence {
  std::size_t length;
  char32_t code_point;
  bool valid;
};

[[nodiscard]] Utf8Sequence next_utf8_sequence(std::string_view text, std::size_t at) noexcept;

// Appends the UTF-8 encoding of a Unicode scalar value (not a surrogate).
void append_utf8(std::string& out, char32_t code_point);

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_UTF8_SEQUENCE_HPP
