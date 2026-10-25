// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TextString reads a PDF text string (ISO 32000-1 section 7.9.2.2, read) as
// UTF-8: UTF-16BE after the byte-order mark FE FF, UTF-8 after EF BB BF
// (ISO 32000-2 section 7.9.2.2, recalled), and PDFDocEncoding otherwise
// through the fonts layer's Annex D table and the glyph list. A UTF-16LE mark
// FF FE is read as little-endian, as pdf.js reads it (recalled), because
// producers write it although the specification does not. An unpaired
// surrogate and an unmapped PDFDoc code are dropped, never written as U+FFFD.
// The /ActualText, /Alt and /E strings of marked content are read here.

#ifndef PDFKIT_TEXT_TEXT_STRING_HPP
#define PDFKIT_TEXT_TEXT_STRING_HPP

#include <string>
#include <string_view>

namespace pdfkit::text {

class TextString {
 public:
  [[nodiscard]] static std::string to_utf8(std::string_view bytes);
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_TEXT_STRING_HPP
