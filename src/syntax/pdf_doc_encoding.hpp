// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PdfDocEncoding reads and writes PDF text strings (ISO 32000-1 section
// 7.9.2.2 and Annex D table D.2, read): UTF-16BE after the byte-order mark
// FE FF, UTF-8 after EF BB BF (ISO 32000-2 section 7.9.2.2, recalled), and
// PDFDocEncoding otherwise. A UTF-16LE mark FF FE is read as little-endian,
// as pdf.js reads it (recalled), because producers write it although the
// specification does not; the reading names the form it found so the
// metadata reader can count it. The metadata reader and writer use it, and
// the writer layer cannot reach the fonts layer's Annex D names, so the
// table is held here; the unit tier checks it code by code against the text
// layer's reading through those names. An unmapped PDFDoc code and an
// unpaired surrogate are dropped, as the text layer drops them.

#ifndef PDFKIT_SYNTAX_PDF_DOC_ENCODING_HPP
#define PDFKIT_SYNTAX_PDF_DOC_ENCODING_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pdfkit::syntax {

class PdfDocEncoding {
 public:
  enum class Form : std::uint8_t { pdf_doc, utf16be, utf16le, utf8 };

  struct Decoded {
    std::string text;
    Form form = Form::pdf_doc;
  };

  // The text string bytes as UTF-8, with the form they were written in.
  [[nodiscard]] static Decoded decode(std::string_view bytes);

  // The PDFDocEncoding bytes of UTF-8 text, or no value when a character
  // has no PDFDoc code (or the text is not well-formed UTF-8).
  [[nodiscard]] static std::optional<std::string> encode(std::string_view utf8);

  // UTF-8 text as UTF-16BE with the byte-order mark; an ill-formed sequence
  // is written as U+FFFD.
  [[nodiscard]] static std::string utf16be(std::string_view utf8);

  // The text string a writer stores for UTF-8 text: PDFDocEncoding when
  // every character has a code, UTF-16BE with the mark otherwise.
  [[nodiscard]] static std::string text_string(std::string_view utf8);

  // "pdf_doc", "utf16be", "utf16le" or "utf8".
  [[nodiscard]] static std::string_view name(Form form) noexcept;

  // The Unicode value of one PDFDoc code; 0 for a code the table leaves
  // undefined.
  [[nodiscard]] static char32_t code_point(std::uint8_t code) noexcept;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_PDF_DOC_ENCODING_HPP
