// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TextReader runs the text layer's passes over a page in their fixed order,
// docs/DESIGN.md chapters 6 to 8: the content interpreter, the /ActualText
// rung, the hidden-text classes (the duplicate fold among them), then the
// layout of the visible glyphs and of the hidden glyphs, and the counts and
// warnings of the page. It holds what is shared across the pages of one
// document: the font loader's cache, the decoded forms, the optional-content
// configuration and the structure tree. One reader serves one document on
// one thread (CLAUDE.md section 3.3).

#ifndef PDFKIT_TEXT_TEXT_READER_HPP
#define PDFKIT_TEXT_TEXT_READER_HPP

#include "fonts/font_loader.hpp"
#include "syntax/document.hpp"
#include "text/content_interpreter.hpp"
#include "text/optional_content.hpp"
#include "text/page_text.hpp"
#include "text/structure_order.hpp"

#include <cstdint>
#include <memory>

namespace pdfkit::text {

class TextReader {
 public:
  // document must outlive the reader.
  explicit TextReader(syntax::Document& document);

  TextReader(const TextReader&) = delete;
  TextReader& operator=(const TextReader&) = delete;
  TextReader(TextReader&&) = delete;
  TextReader& operator=(TextReader&&) = delete;
  ~TextReader() = default;

  // Reads page number (1-based, within the document's pages).
  [[nodiscard]] PageText read(std::uint32_t number);

  // The interpreter alone, for a caller that needs the records and not the
  // layout (the image listing, the surgery).
  [[nodiscard]] PageContent interpret(std::uint32_t number);

 private:
  syntax::Document* document_;
  fonts::FontLoader loader_;
  OptionalContent optional_content_;
  ContentInterpreter interpreter_;
  std::unique_ptr<StructureOrder> structure_;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_TEXT_READER_HPP
