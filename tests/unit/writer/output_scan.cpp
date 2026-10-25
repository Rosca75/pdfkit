// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The writes go through the C ABI, the surface every caller uses; the scan
// opens the output with the library's own reader and decodes every object's
// stream, the extractor's view of the file.

#include "writer/output_scan.hpp"

#include "pdfkit/pdfkit.h"

#include "platform/byte_span.hpp"
#include "syntax/limits.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"

#include <doctest.h>

#include <stdexcept>

namespace pdfkit::test {

namespace {

struct Written {
  std::string pdf;
  std::string report;
};

Written write(const std::string& file, const std::vector<std::uint32_t>& pages) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  pdfkit_status status = pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()),
                                     file.size(), nullptr, &doc, &out);
  const std::string open_text(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  INFO(open_text);
  REQUIRE(status == PDFKIT_OK);
  pdfkit_edit* edit = nullptr;
  status = pdfkit_edit_new(doc, &edit, &out);
  pdfkit_buffer_free(&out);
  REQUIRE(status == PDFKIT_OK);
  if (!pages.empty()) {
    std::string json = "{\"pages\": [";
    for (std::size_t i = 0; i < pages.size(); ++i) {
      json += (i == 0 ? "" : ", ") + std::to_string(pages[i]);
    }
    json += "]}";
    status = pdfkit_select_pages(edit, json.c_str(), &out);
    const std::string text(reinterpret_cast<const char*>(out.data), out.size);
    pdfkit_buffer_free(&out);
    INFO(text);
    REQUIRE(status == PDFKIT_OK);
  }
  pdfkit_buffer pdf{nullptr, 0, nullptr};
  status = pdfkit_write(edit, nullptr, &pdf, &out);
  Written written;
  written.report.assign(reinterpret_cast<const char*>(out.data), out.size);
  written.pdf.assign(reinterpret_cast<const char*>(pdf.data), pdf.size);
  pdfkit_buffer_free(&out);
  pdfkit_buffer_free(&pdf);
  pdfkit_edit_free(edit);
  pdfkit_close(doc);
  INFO(written.report);
  REQUIRE(status == PDFKIT_OK);
  return written;
}

}  // namespace

std::string identity_write(const std::string& file) {
  return write(file, {}).pdf;
}

std::string select_write(const std::string& file, const std::vector<std::uint32_t>& pages) {
  return write(file, pages).pdf;
}

std::string write_report(const std::string& file, const std::vector<std::uint32_t>& pages) {
  return write(file, pages).report;
}

std::string everything(const std::string& output) {
  std::string out = output;
  const auto document = syntax::Document::open(platform::as_bytes(output), syntax::Limits{});
  for (const auto& entry : document->entries()) {
    const syntax::Object value = document->object({entry.first, 0});
    if (const syntax::Stream* stream = value.stream()) {
      const auto decoded = document->decode(*stream);
      out += '\n';
      out.append(reinterpret_cast<const char*>(decoded.bytes.data()), decoded.bytes.size());
    }
  }
  return out;
}

bool contains_marker(const std::string& output, std::string_view marker) {
  return everything(output).find(marker) != std::string::npos;
}

std::unique_ptr<syntax::Document> reopen(const std::string& output) {
  auto document = syntax::Document::open(platform::as_bytes(output), syntax::Limits{});
  if (!document->repairs().empty()) {
    throw std::runtime_error("the output needed a repair: " + document->repairs().front());
  }
  return document;
}

}  // namespace pdfkit::test
