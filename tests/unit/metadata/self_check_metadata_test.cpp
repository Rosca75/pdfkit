// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the self-check's metadata needles: the original value of a
// rewritten field planted in a second place is removed with the rewrite when
// no function could rewrite it (a custom Info key, an XMP property no field
// pairs with) and is found and refused, naming the surface, where it stays
// (a field the caller did not set, the page text, a content stream's
// comment); a rewrite whose originals are nowhere else passes and says so in
// the report.

#include "metadata/support/metadata_fixture.hpp"
#include "redact/self_check.hpp"
#include "writer/output_scan.hpp"

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

TEST_CASE("a custom Info key or XMP property repeating an original goes with the rewrite") {
  MetadataFixture made = fixture();
  made.info += " /Company (Quarterly Zebulon Report) /Nested [(x) (Ann Planted)] /Kept (Other)";
  made.packet = MetadataFixture::xmp(
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>Quarterly Zebulon Report</rdf:li>"
      "</rdf:Alt></dc:title>\n"
      "<my:Copy xmlns:my='urn:example:custom'>A quarterly ZEBULON report copy</my:Copy>\n"
      "<my:Other xmlns:my='urn:example:custom'>Unrelated</my:Other>\n",
      "xmp:MetadataDate='Quarterly Zebulon Report'");
  const MetadataFixture::Write written = MetadataFixture::write(
      made.build(), R"({"Title": "REDACTED TITLE", "Author": "REDACTED AUTHOR"})");
  INFO(written.report);
  REQUIRE(written.write_status == PDFKIT_OK);
  const std::string all = pdfkit::test::everything(written.pdf);
  CHECK(all.find("Company") == std::string::npos);
  CHECK(all.find("Nested") == std::string::npos);
  CHECK(all.find("my:Copy") == std::string::npos);
  CHECK(all.find("MetadataDate") == std::string::npos);
  CHECK(all.find("/Kept") != std::string::npos);
  CHECK(all.find("<my:Other xmlns:my='urn:example:custom'>Unrelated</my:Other>") !=
        std::string::npos);
}

TEST_CASE("an original value a field not rewritten repeats is refused in the info and xmp") {
  // A field the caller did not set keeps its value, so its repeat is refused
  // and the caller sets that field too.
  MetadataFixture made = fixture();
  made.info += " /Subject (Quarterly Zebulon Report)";
  check_refused(made, R"({"Title": "REDACTED TITLE"})", "info");

  MetadataFixture packet = fixture();
  packet.packet = MetadataFixture::xmp(
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>Quarterly Zebulon Report</rdf:li>"
      "</rdf:Alt></dc:title>\n"
      "<dc:description><rdf:Alt><rdf:li xml:lang='x-default'>Quarterly Zebulon Report</rdf:li>"
      "</rdf:Alt></dc:description>\n");
  check_refused(packet, R"({"Title": "REDACTED TITLE"})", "xmp");
}

TEST_CASE("a text redaction removes a custom Info key or XMP property holding its string") {
  MetadataFixture made = fixture();
  made.content = "BT /F1 12 Tf 72 700 Td (Body Wombat text) Tj ET";
  made.info += " /Company (The Wombat Company)";
  made.packet = MetadataFixture::xmp(
      "<dc:rights><rdf:Alt><rdf:li xml:lang='x-default'>Wombat rights</rdf:li></rdf:Alt>"
      "</dc:rights>\n");
  const MetadataFixture::Write written = MetadataFixture::write(
      made.build(), "", R"({"matches": [{"search": "wombat", "placeholder": "[W]"}]})");
  INFO(written.report);
  REQUIRE(written.write_status == PDFKIT_OK);
  const std::string all = pdfkit::test::everything(written.pdf);
  CHECK(all.find("Company") == std::string::npos);
  CHECK(all.find("dc:rights") == std::string::npos);
  CHECK(all.find("Quarterly Zebulon Report") != std::string::npos);
}

TEST_CASE("a namespace URI of the packet is not text the self-check searches") {
  // "adobe" is in http://ns.adobe.com/xap/1.0/, the namespace of xmp:.
  MetadataFixture made = fixture();
  made.content = "BT /F1 12 Tf 72 700 Td (Made with adobe tools) Tj ET";
  const MetadataFixture::Write written = MetadataFixture::write(
      made.build(), "", R"({"matches": [{"search": "adobe", "placeholder": "[A]"}]})");
  INFO(written.report);
  CHECK(written.write_status == PDFKIT_OK);
}

TEST_CASE("an original of fewer than three letters or digits is not searched for") {
  MetadataFixture made;
  made.info = "/Author (A.B)";
  made.content = "BT /F1 12 Tf 72 700 Td (A.B wrote this) Tj ET";
  const MetadataFixture::Write written =
      MetadataFixture::write(made.build(), R"({"Author": "REDACTED AUTHOR"})");
  INFO(written.report);
  CHECK(written.write_status == PDFKIT_OK);
}

TEST_CASE("an original value in the page text or a content stream's comment is refused") {
  MetadataFixture text = fixture();
  text.content = "BT /F1 12 Tf 72 700 Td (By Ann Planted) Tj ET";
  check_refused(text, R"({"Author": "REDACTED AUTHOR"})", "text");

  MetadataFixture comment = fixture();
  comment.content = "% drawn for Ann Planted\nBT /F1 12 Tf 72 700 Td (Body) Tj ET";
  check_refused(comment, R"({"Author": "REDACTED AUTHOR"})", "stream");
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
