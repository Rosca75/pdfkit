// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The summary is plain text so a failing comparison prints both readings.

#include "support/document_summary.hpp"

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/page.hpp"

#include <charconv>

namespace pdfkit::test {

namespace {

std::string number(double value) {
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return {buffer.data(), result.ptr};
}

std::string box(const syntax::Page::Box& value) {
  return number(value.left) + " " + number(value.bottom) + " " + number(value.right) + " " +
         number(value.top);
}

std::string keys(const syntax::Object& value) {
  std::string out;
  if (const syntax::Dictionary* dictionary = value.dictionary()) {
    for (const auto& entry : dictionary->entries()) {
      out += "/" + entry.first;
    }
  }
  return out;
}

}  // namespace

std::unique_ptr<syntax::Document> open_document(const std::string& file,
                                                const syntax::Limits& limits) {
  return syntax::Document::open(platform::as_bytes(file), limits);
}

std::string page_text(syntax::Document& document, std::uint32_t number) {
  const syntax::Page& page = document.pages().at(number - 1);
  std::string out;
  for (const auto& piece : document.content_pieces(page.contents, page.number)) {
    for (const std::byte value : piece) {
      out.push_back(static_cast<char>(value));
    }
  }
  return out;
}

std::string summarise(syntax::Document& document) {
  std::string out = "pages " + std::to_string(document.pages().size()) + "\n";
  out += "catalog " + keys(document.catalog()) + "\n";
  for (const syntax::Page& page : document.pages()) {
    out += "page " + std::to_string(page.number) + " media " + box(page.media_box) + " crop " +
           box(page.crop_box) + " rotate " + std::to_string(page.rotate) + "\n";
    out += "  content " + page_text(document, page.number) + "\n";
  }
  return out;
}

}  // namespace pdfkit::test
