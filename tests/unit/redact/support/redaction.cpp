// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The redaction through the C ABI, each status kept for the test to judge,
// and the output read back with the library's reader (no repair allowed:
// the writer's output must open as written).

#include "redact/support/redaction.hpp"

#include "platform/byte_span.hpp"
#include "syntax/document.hpp"
#include "syntax/limits.hpp"
#include "text/text_reader.hpp"

#include <doctest.h>

namespace pdfkit::test {

namespace {

std::string take(pdfkit_buffer& buffer) {
  std::string text(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  return text;
}

}  // namespace

Redaction redact(const std::string& file, const std::string& matches_json,
                 const std::vector<std::uint32_t>& pages) {
  Redaction result;
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  pdfkit_status status = pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()),
                                     file.size(), nullptr, &doc, &out);
  const std::string opened = take(out);
  INFO(opened);
  REQUIRE(status == PDFKIT_OK);
  pdfkit_edit* edit = nullptr;
  status = pdfkit_edit_new(doc, &edit, &out);
  (void)take(out);
  REQUIRE(status == PDFKIT_OK);
  result.match_status = pdfkit_redact_text(edit, matches_json.c_str(), &out);
  result.matches = take(out);
  if (result.match_status == PDFKIT_OK && !pages.empty()) {
    std::string json = "{\"pages\": [";
    for (std::size_t i = 0; i < pages.size(); ++i) {
      json += (i == 0 ? "" : ", ") + std::to_string(pages[i]);
    }
    json += "]}";
    status = pdfkit_select_pages(edit, json.c_str(), &out);
    (void)take(out);
    REQUIRE(status == PDFKIT_OK);
  }
  if (result.match_status == PDFKIT_OK) {
    pdfkit_buffer pdf{nullptr, 0, nullptr};
    result.write_status = pdfkit_write(edit, nullptr, &pdf, &out);
    result.report = take(out);
    result.pdf = take(pdf);
  }
  pdfkit_edit_free(edit);
  pdfkit_close(doc);
  return result;
}

std::vector<text::PageText> read_pages(const std::string& output) {
  std::unique_ptr<syntax::Document> document =
      syntax::Document::open(platform::as_bytes(output), syntax::Limits{});
  REQUIRE(document != nullptr);
  CHECK(document->repairs().empty());
  text::TextReader reader(*document);
  std::vector<text::PageText> pages;
  for (const syntax::Page& page : document->pages()) {
    pages.push_back(reader.read(page.number));
  }
  return pages;
}

std::string page_text(const std::string& output, std::uint32_t number) {
  const std::vector<text::PageText> pages = read_pages(output);
  REQUIRE(number >= 1);
  REQUIRE(number <= pages.size());
  return pages[number - 1].text();
}

std::string search(const std::string& text, const std::string& placeholder) {
  return R"({"matches": [{"search": ")" + text + R"(", "placeholder": ")" + placeholder + R"("}]})";
}

}  // namespace pdfkit::test
