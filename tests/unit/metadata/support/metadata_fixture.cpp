// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The fixture's file: a catalog, one page with a Helvetica font, the Info
// dictionary and the packet, built by the test tree's PDF builder.

#include "metadata/support/metadata_fixture.hpp"

#include "support/pdf_builder.hpp"

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

}  // namespace pdfkit::test
