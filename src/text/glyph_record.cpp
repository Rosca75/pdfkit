// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The one question a glyph record answers about itself: whether it is a
// space, which ends a word in the layout (docs/DESIGN.md chapter 7). White
// space is the ASCII set, the no-break space and the Unicode spaces U+2000
// to U+200B, U+3000 and U+205F, U+202F (recalled from the Unicode White_Space
// property).

#include "text/glyph_record.hpp"

#include "platform/utf8_sequence.hpp"

namespace pdfkit::text {

namespace {

[[nodiscard]] bool white(char32_t c) noexcept {
  return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f' || c == 0x0B ||
         c == 0xA0 || (c >= 0x2000 && c <= 0x200B) || c == 0x202F || c == 0x205F || c == 0x3000;
}

}  // namespace

bool GlyphRecord::is_space() const noexcept {
  if (text.empty()) {
    return false;
  }
  std::size_t at = 0;
  while (at < text.size()) {
    const platform::Utf8Sequence unit = platform::next_utf8_sequence(text, at);
    if (!unit.valid || !white(unit.code_point)) {
      return false;
    }
    at += unit.length;
  }
  return true;
}

}  // namespace pdfkit::text
