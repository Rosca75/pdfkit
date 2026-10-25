// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The text fixture: three fonts in every page's resources, the document
// opened once, and every interpretation checked against the serialiser.

#include "text/support/text_document.hpp"

#include "support/document_summary.hpp"
#include "text/content_interpreter.hpp"
#include "text/content_serialiser.hpp"

#include <doctest.h>

#include <stdexcept>

namespace pdfkit::test {

namespace {

// A descendant CIDFontType2 with /W giving code 0x20 250, 0x41 600, 0x42
// 610 and 0x4E2D 1000; /DW 1000 for the rest.
std::string composite(std::string_view encoding) {
  return "<< /Type /Font /Subtype /Type0 /BaseFont /TestCID /Encoding /" + std::string(encoding) +
         " /ToUnicode /Identity-H /DescendantFonts [<< /Type /Font /Subtype /CIDFontType2 "
         "/BaseFont /TestCID /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) "
         "/Supplement 0 >> /DW 1000 /W [32 [250] 65 [600 610]] >>] >>";
}

}  // namespace

TextDocument::TextDocument() {
  builder_.begin_document();
  helvetica_ = builder_.add(
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
  identity_h_ = builder_.add(composite("Identity-H"));
  identity_v_ = builder_.add(composite("Identity-V"));
}

std::uint32_t TextDocument::add(std::string body) {
  return builder_.add(std::move(body));
}

std::uint32_t TextDocument::add_stream(std::string entries, std::string data) {
  return builder_.add_stream(std::move(entries), std::move(data));
}

std::uint32_t TextDocument::helvetica() {
  return helvetica_;
}

std::uint32_t TextDocument::identity_h() {
  return identity_h_;
}

std::uint32_t TextDocument::identity_v() {
  return identity_v_;
}

std::uint32_t TextDocument::page(const std::string& content, const std::string& resources,
                                 const std::string& extra, const std::string& fonts) {
  const std::string font_entries = "/Font << /F1 " + std::to_string(helvetica_) + " 0 R /F2 " +
                                   std::to_string(identity_h_) + " 0 R /F3 " +
                                   std::to_string(identity_v_) + " 0 R " + fonts + " >> ";
  return builder_.add_page(content, "/Resources << " + font_entries + resources + " >> " + extra);
}

std::string TextDocument::bytes() {
  if (!catalog_entries_.empty()) {
    builder_.set(builder_.catalog(), "<< /Type /Catalog /Pages " +
                                         std::to_string(builder_.page_tree()) + " 0 R " +
                                         catalog_entries_ + " >>");
  }
  return builder_.build();
}

void TextDocument::open() {
  if (document_) {
    return;
  }
  if (!catalog_entries_.empty()) {
    builder_.set(builder_.catalog(), "<< /Type /Catalog /Pages " +
                                         std::to_string(builder_.page_tree()) + " 0 R " +
                                         catalog_entries_ + " >>");
  }
  document_ = open_document(builder_.build());
  loader_ = std::make_unique<fonts::FontLoader>(*document_);
  optional_content_ = std::make_unique<text::OptionalContent>(*document_);
}

syntax::Document& TextDocument::document() {
  open();
  return *document_;
}

text::PageContent TextDocument::interpret(std::uint32_t number) {
  open();
  if (number == 0 || number > document_->pages().size()) {
    throw std::out_of_range("no such fixture page");
  }
  text::ContentInterpreter interpreter(*document_, *loader_, *optional_content_);
  text::PageContent content = interpreter.run(document_->pages()[number - 1]);
  for (std::size_t i = 0; i < content.streams.size(); ++i) {
    const text::ContentSerialiser::Result written =
        text::ContentSerialiser::write(content.streams[i], i, content.glyphs);
    INFO("walk " << i << " of page " << number);
    CHECK(written.mismatches.empty());
    CHECK(written.bytes == *content.streams[i].bytes);
  }
  return content;
}

text::PageText TextDocument::read(std::uint32_t number) {
  (void)interpret(number);
  if (reader_ == nullptr) {
    reader_ = std::make_unique<text::TextReader>(*document_);
  }
  return reader_->read(number);
}

std::string TextDocument::drawn_text(const text::PageContent& content) {
  std::string out;
  for (const text::GlyphRecord& record : content.glyphs) {
    out += record.text;
  }
  return out;
}

}  // namespace pdfkit::test
