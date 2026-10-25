// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the self-check's metadata needles: the original value of a
// rewritten field planted in a second place (a custom Info key, a string
// object a page holds, a custom XMP element, the page text) is found and the
// write refused naming the surface; a rewrite whose originals are nowhere
// else passes and says so in the report.

#include "metadata/support/metadata_fixture.hpp"
#include "redact/self_check.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::test::MetadataFixture;

MetadataFixture fixture() {
  MetadataFixture made;
  made.info = "/Title (Quarterly Zebulon Report) /Author (Ann Planted)";
  made.packet = MetadataFixture::xmp(
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>Quarterly Zebulon Report</rdf:li>"
      "</rdf:Alt></dc:title>\n");
  return made;
}

void check_refused(const MetadataFixture& made, const std::string& fields,
                   const std::string& surface) {
  const MetadataFixture::Write written = MetadataFixture::write(made.build(), fields);
  INFO(written.report);
  REQUIRE(written.set_status == PDFKIT_OK);
  CHECK(written.write_status == PDFKIT_E_SELF_CHECK);
  CHECK(written.pdf.empty());
  CHECK(written.report.find("in the " + surface) != std::string::npos);
}

}  // namespace

TEST_CASE("a rewrite whose originals are nowhere else passes the self-check") {
  const MetadataFixture::Write written =
      MetadataFixture::write(fixture().build(), R"({"Title": "REDACTED TITLE"})");
  INFO(written.report);
  REQUIRE(written.write_status == PDFKIT_OK);
  CHECK(written.report.find("\"status\":\"passed\"") != std::string::npos);
}

TEST_CASE("an original value left in a custom Info key is refused as info") {
  MetadataFixture made = fixture();
  made.info += " /Company (Quarterly Zebulon Report)";
  check_refused(made, R"({"Title": "REDACTED TITLE"})", "info");
}

TEST_CASE("an original value left in a custom XMP element is refused as xmp") {
  MetadataFixture made = fixture();
  made.packet = MetadataFixture::xmp(
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>Quarterly Zebulon Report</rdf:li>"
      "</rdf:Alt></dc:title>\n"
      "<my:Copy xmlns:my='urn:example:custom'>Quarterly zebulon report</my:Copy>\n");
  check_refused(made, R"({"Title": "REDACTED TITLE"})", "xmp");
}

TEST_CASE("an original value in the page text or a page's string object is refused") {
  MetadataFixture text = fixture();
  text.content = "BT /F1 12 Tf 72 700 Td (By Ann Planted) Tj ET";
  check_refused(text, R"({"Author": "REDACTED AUTHOR"})", "text");

  MetadataFixture object = fixture();
  object.content = "BT /F1 12 Tf 72 700 Td (Body) Tj ET";
  object.info += " /Nested [(Ann Planted)]";
  check_refused(object, R"({"Author": "REDACTED AUTHOR"})", "info");
}

TEST_CASE("the original value removed from the page text by a text redaction passes") {
  MetadataFixture made = fixture();
  made.content = "BT /F1 12 Tf 72 700 Td (By Ann Planted) Tj ET";
  const MetadataFixture::Write written =
      MetadataFixture::write(made.build(), R"({"Author": "REDACTED AUTHOR"})",
                             R"({"matches": [{"search": "Ann Planted", "placeholder": "[A]"}]})");
  INFO(written.report);
  CHECK(written.write_status == PDFKIT_OK);
}
