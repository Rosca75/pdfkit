// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the cross-reference stream and the file around it: every offset
// lands on its "n 0 obj", the offset width is the narrowest that holds the
// largest offset, the output opens through the library's own
// cross-reference reader without a repair, the header carries the right
// version and a binary comment, and the /ID is the file's own hash.

#include "writer/xref_stream_writer.hpp"

#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/cross_reference.hpp"
#include "syntax/document.hpp"
#include "writer/document_id.hpp"
#include "writer/file_writer.hpp"
#include "writer/object_graph.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::test::PdfBuilder;
using pdfkit::writer::XrefStreamWriter;

std::string write_identity(const std::string& file) {
  const auto document = pdfkit::test::open_document(file);
  pdfkit::writer::ObjectGraph graph(*document);
  return pdfkit::writer::FileWriter::write(graph).bytes;
}

std::string sample(const std::string& version = "1.7") {
  PdfBuilder builder;
  builder.begin_document();
  for (int i = 0; i < 3; ++i) {
    (void)builder.add_page("BT /F1 12 Tf 72 720 Td (page " + std::to_string(i + 1) + ") Tj ET", "",
                           i % 2 == 0);
  }
  PdfBuilder::Layout layout;
  layout.version = version;
  return builder.build(layout);
}

}  // namespace

TEST_CASE("the narrowest offset width") {
  CHECK(XrefStreamWriter::width_for(0) == 1);
  CHECK(XrefStreamWriter::width_for(255) == 1);
  CHECK(XrefStreamWriter::width_for(256) == 2);
  CHECK(XrefStreamWriter::width_for(65535) == 2);
  CHECK(XrefStreamWriter::width_for(65536) == 3);
  CHECK(XrefStreamWriter::width_for(0x100000000ULL) == 5);
}

TEST_CASE("every offset lands on its object and the output opens without a repair") {
  const std::string output = write_identity(sample());
  const auto document = pdfkit::test::reopen(output);
  CHECK_FALSE(document->reconstructed());
  CHECK(document->pages().size() == 3);
  CHECK(document->trailer().get("Root").reference().has_value());
  for (const auto& [number, entry] : document->entries()) {
    if (entry.type != pdfkit::syntax::CrossReference::Entry::Type::offset) {
      continue;
    }
    const std::string header = std::to_string(number) + " 0 obj\n";
    CHECK(output.compare(entry.offset, header.size(), header) == 0);
  }
  // The file is small, so two bytes hold every offset.
  CHECK(output.find("/W [1 2 2]") != std::string::npos);
  CHECK(output.starts_with("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n"));
  CHECK(output.ends_with("\n%%EOF\n"));
}

TEST_CASE("the header version is at least 1.5") {
  CHECK(write_identity(sample("1.3")).starts_with("%PDF-1.5\n"));
  CHECK(write_identity(sample("2.0")).starts_with("%PDF-2.0\n"));
  CHECK(pdfkit::writer::FileWriter::header_version("") == "1.5");
  CHECK(pdfkit::writer::FileWriter::header_version("1.6") == "1.6");
}

TEST_CASE("the /ID is the hash of the output written with a zero placeholder") {
  std::string output = write_identity(sample());
  const std::size_t at = output.find("/ID [<");
  REQUIRE(at != std::string::npos);
  const std::size_t first = at + 6;
  const std::string id = output.substr(first, pdfkit::writer::DocumentId::digits);
  const std::size_t second = first + pdfkit::writer::DocumentId::digits + 3;
  CHECK(output.substr(second, pdfkit::writer::DocumentId::digits) == id);
  output.replace(first, id.size(), pdfkit::writer::DocumentId::placeholder());
  output.replace(second, id.size(), pdfkit::writer::DocumentId::placeholder());
  CHECK(pdfkit::writer::DocumentId::patch(output, first, second) == id);
  CHECK(id != pdfkit::writer::DocumentId::placeholder());
}
