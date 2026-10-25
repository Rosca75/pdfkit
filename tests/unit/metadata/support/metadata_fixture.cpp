// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The fixture's file: a catalog, one page with a Helvetica font, the Info
// dictionary and the packet, built by the test tree's PDF builder.

#include "metadata/support/metadata_fixture.hpp"

#include "support/pdf_builder.hpp"

#include <doctest.h>

#include <cstdint>

namespace pdfkit::test {

std::string MetadataFixture::xmp(const std::string& body, const std::string& attributes) {
  return "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
         "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
         "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
         "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
         "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\" "
         "xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"" +
         (attributes.empty() ? std::string() : " " + attributes) + ">\n" + body +
         "</rdf:Description>\n</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>";
}

std::string MetadataFixture::build() const {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t font = builder.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  (void)builder.add_page(content,
                         "/Resources << /Font << /F1 " + std::to_string(font) + " 0 R >> >>");
  if (!info.empty()) {
    builder.set_info(builder.add("<< " + info + " >>"));
  }
  if (!packet.empty()) {
    const std::uint32_t metadata = builder.add_stream("/Type /Metadata /Subtype /XML", packet);
    std::string catalog = "<< /Type /Catalog /Pages " + std::to_string(builder.page_tree()) +
                          " 0 R /Metadata " + std::to_string(metadata) + " 0 R >>";
    builder.set(builder.catalog(), catalog);
  }
  if (encryption) {
    builder.encrypt(*encryption);
  }
  return builder.build();
}

MetadataFixture::Write MetadataFixture::write(const std::string& file, const std::string& fields,
                                              const std::string& matches) {
  const auto take = [](pdfkit_buffer& buffer) {
    std::string text(reinterpret_cast<const char*>(buffer.data), buffer.size);
    pdfkit_buffer_free(&buffer);
    return text;
  };
  Write result;
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
  if (!matches.empty()) {
    status = pdfkit_redact_text(edit, matches.c_str(), &out);
    const std::string answer = take(out);
    INFO(answer);
    REQUIRE(status == PDFKIT_OK);
  }
  if (!fields.empty()) {
    result.set_status = pdfkit_set_metadata(edit, fields.c_str(), &out);
    result.set_answer = take(out);
  }
  if (result.set_status == PDFKIT_OK) {
    pdfkit_buffer pdf{nullptr, 0, nullptr};
    result.write_status = pdfkit_write(edit, nullptr, &pdf, &out);
    result.report = take(out);
    result.pdf = take(pdf);
  }
  pdfkit_edit_free(edit);
  pdfkit_close(doc);
  return result;
}

}  // namespace pdfkit::test
