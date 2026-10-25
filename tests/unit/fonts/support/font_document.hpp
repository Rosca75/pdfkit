// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FontDocument is the fixture the fonts tests share: a PdfBuilder to which a
// test adds font dictionaries, programs and maps as objects, then a one-page
// document opened with the library's reader and a FontLoader over it, so a
// test reads as the font it declares and asks the loaded font what it
// decodes. decoded() joins the text of an operand's codes and lists their
// rungs, which is what most assertions compare.

#ifndef PDFKIT_TESTS_UNIT_FONTS_SUPPORT_FONT_DOCUMENT_HPP
#define PDFKIT_TESTS_UNIT_FONTS_SUPPORT_FONT_DOCUMENT_HPP

#include "fonts/decoding_rung.hpp"
#include "fonts/font_loader.hpp"
#include "fonts/pdf_font.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/document.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pdfkit::test {

class FontDocument {
 public:
  struct Decoded {
    std::string text;
    std::vector<fonts::DecodingRung> rungs;
    std::vector<std::uint32_t> codes;
  };

  FontDocument();

  // Adds an object (a font dictionary, a descriptor, an encoding).
  std::uint32_t add(std::string body);
  // Adds a stream object with its data written as given (binary safe).
  std::uint32_t add_stream(std::string entries, std::string data, bool flate = false);
  // Writes a page whose content is content and whose resources are
  // resources, then opens the document; later calls to add are ignored.
  void open(const std::string& content = "", const std::string& resources = "");

  // The font of object number (opening the document first if needed).
  [[nodiscard]] std::shared_ptr<fonts::PdfFont> load(std::uint32_t number);
  [[nodiscard]] syntax::Document& document();

  // The codes of bytes decoded in font.
  [[nodiscard]] static Decoded decoded(fonts::PdfFont& font, const std::string& bytes);

 private:
  PdfBuilder builder_;
  std::unique_ptr<syntax::Document> document_;
  std::unique_ptr<fonts::FontLoader> loader_;
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_UNIT_FONTS_SUPPORT_FONT_DOCUMENT_HPP
