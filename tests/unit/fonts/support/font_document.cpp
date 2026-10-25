// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The document is opened lazily, on the first load, so a test adds every
// object it needs before it asks for a font.

#include "support/font_document.hpp"

#include "platform/byte_span.hpp"
#include "support/document_summary.hpp"
#include "syntax/object.hpp"

namespace pdfkit::test {

FontDocument::FontDocument() {
  builder_.begin_document();
}

std::uint32_t FontDocument::add(std::string body) {
  return builder_.add(std::move(body));
}

std::uint32_t FontDocument::add_stream(std::string entries, std::string data, bool flate) {
  StreamOptions options;
  options.flate = flate;
  return builder_.add_stream(std::move(entries), std::move(data), options);
}

void FontDocument::open(const std::string& content, const std::string& resources) {
  if (document_) {
    return;
  }
  builder_.add_page(content, resources.empty() ? "" : "/Resources " + resources);
  document_ = open_document(builder_.build());
  loader_ = std::make_unique<fonts::FontLoader>(*document_);
}

std::shared_ptr<fonts::PdfFont> FontDocument::load(std::uint32_t number) {
  open();
  return loader_->load(syntax::Object::make_reference({number, 0}));
}

syntax::Document& FontDocument::document() {
  open();
  return *document_;
}

FontDocument::Decoded FontDocument::decoded(fonts::PdfFont& font, const std::string& bytes) {
  Decoded out;
  for (const fonts::PdfFont::DecodedCode& code : font.decode(platform::as_bytes(bytes))) {
    out.text += code.text;
    out.rungs.push_back(code.rung);
    out.codes.push_back(code.code);
  }
  return out;
}

}  // namespace pdfkit::test
