// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TextDocument is the fixture the text tests share: a PdfBuilder holding the
// fonts a test asks for (Helvetica from the Core 14 metrics, a two-byte
// Identity-H font and an Identity-V font with stated widths and a /ToUnicode
// that reads each code as itself), the objects it adds and the pages it
// draws, opened with the library's reader. interpret() runs the content
// interpreter over one page and, before returning, writes every walk of the
// page back through the serialiser and requires the bytes to be identical
// with no mismatch, so every fixture page of the text tests is also a case of
// the re-serialisation identity. read() runs the whole text layer over a
// page.

#ifndef PDFKIT_TESTS_UNIT_TEXT_SUPPORT_TEXT_DOCUMENT_HPP
#define PDFKIT_TESTS_UNIT_TEXT_SUPPORT_TEXT_DOCUMENT_HPP

#include "fonts/font_loader.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/document.hpp"
#include "text/optional_content.hpp"
#include "text/page_content.hpp"
#include "text/page_text.hpp"
#include "text/text_reader.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pdfkit::test {

class TextDocument {
 public:
  TextDocument();

  std::uint32_t add(std::string body);
  std::uint32_t add_stream(std::string entries, std::string data);
  // Adds catalog entries (before open).
  void catalog_entries(std::string entries) { catalog_entries_ = std::move(entries); }
  // Adds an information dictionary of body, named by the trailer (before open).
  void info(std::string body) { builder_.set_info(builder_.add(std::move(body))); }

  // The fonts, each added once on first use; the value is its object number.
  std::uint32_t helvetica();
  std::uint32_t identity_h();
  std::uint32_t identity_v();

  // Adds a page drawing content; resources is the body of its resource
  // dictionary; extra holds further page entries; fonts holds further
  // entries of its /Font dictionary. Fonts F1 (Helvetica), F2 (Identity-H)
  // and F3 (Identity-V) are always in the resources.
  std::uint32_t page(const std::string& content, const std::string& resources = "",
                     const std::string& extra = "", const std::string& fonts = "");

  void open();
  // The file the fixture writes (the catalog entries applied).
  [[nodiscard]] std::string bytes();
  [[nodiscard]] syntax::Document& document();
  // Interprets page number (1-based), checking the re-serialisation identity
  // of each of its walks.
  [[nodiscard]] text::PageContent interpret(std::uint32_t number = 1);
  // Reads page number through the whole text layer (interpreter, rungs,
  // hidden classes, layout), checking the identity of its walks first.
  [[nodiscard]] text::PageText read(std::uint32_t number = 1);
  // The text of a page's glyphs in draw order.
  [[nodiscard]] static std::string drawn_text(const text::PageContent& content);

 private:
  PdfBuilder builder_;
  std::string catalog_entries_;
  std::uint32_t helvetica_ = 0;
  std::uint32_t identity_h_ = 0;
  std::uint32_t identity_v_ = 0;
  std::unique_ptr<syntax::Document> document_;
  std::unique_ptr<fonts::FontLoader> loader_;
  std::unique_ptr<text::OptionalContent> optional_content_;
  std::unique_ptr<text::TextReader> reader_;
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_UNIT_TEXT_SUPPORT_TEXT_DOCUMENT_HPP
