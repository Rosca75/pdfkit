// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of writer::MetadataWriter through pdfkit_set_metadata and
// pdfkit_write: a written field reads back from both places; a field not set
// keeps both values; a custom namespace survives byte for byte; the history
// goes and the identifiers are regenerated from the output, deterministically;
// a value outside PDFDocEncoding is written UTF-16BE with the mark; a field
// the packet lacks is added; a field set to null is removed from both places;
// dates are written in each place's form; an unreadable packet is dropped
// with its warning; a file without an Info dictionary gains one.

#include "writer/metadata_writer.hpp"

#include "metadata/support/metadata_fixture.hpp"
#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "support/document_summary.hpp"
#include "support/require_value.hpp"
#include "syntax/metadata_reader.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"
#include "syntax/xmp_scanner.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::syntax::MetadataReader;
using pdfkit::syntax::XmpScanner;
using pdfkit::test::MetadataFixture;
using pdfkit::test::open_document;
using pdfkit::test::require_value;
using Field = MetadataReader::Field;

const std::string custom = "<my:Note xmlns:my='urn:example:custom'>Keep &amp; me</my:Note>";

MetadataFixture both() {
  MetadataFixture fixture;
  fixture.info =
      "/Title (Old title) /Author (Old Author) /Producer (Old Maker) /Custom (Kept custom)";
  fixture.packet = MetadataFixture::xmp(
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>Old title</rdf:li></rdf:Alt></dc:title>\n"
      "<dc:creator><rdf:Seq><rdf:li>Old Author</rdf:li></rdf:Seq></dc:creator>\n" +
          custom +
          "\n<xmpMM:DocumentID>uuid:old-document</xmpMM:DocumentID>\n"
          "<xmpMM:History><rdf:Seq><rdf:li rdf:parseType='Resource'><stEvt:action "
          "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#'>saved by Old Author"
          "</stEvt:action></rdf:li></rdf:Seq></xmpMM:History>\n",
      "pdf:Producer='Old Maker' xmpMM:InstanceID='uuid:old-instance'");
  return fixture;
}

MetadataReader::Result reread(const std::string& output) {
  auto document = open_document(output);
  return MetadataReader::read(*document);
}

const MetadataReader::Row& row(const MetadataReader::Result& result, Field field) {
  return result.rows[static_cast<std::size_t>(field)];
}

std::string packet_of(const std::string& output) {
  return reread(output).packet;
}

MetadataFixture::Write write(const std::string& file, const std::string& fields) {
  MetadataFixture::Write written = MetadataFixture::write(file, fields);
  INFO(written.set_answer);
  INFO(written.report);
  REQUIRE(written.set_status == PDFKIT_OK);
  REQUIRE(written.write_status == PDFKIT_OK);
  return written;
}

}  // namespace

TEST_CASE("a written field appears in both places; a field not set keeps both values") {
  const auto written =
      write(both().build(), R"({"Title": "New title", "Author": "Ann; Bob", "Producer": "P2"})");
  const MetadataReader::Result result = reread(written.pdf);
  CHECK(*row(result, Field::title).info == "New title");
  CHECK(row(result, Field::title).xmp.front().value == "New title");
  CHECK(*row(result, Field::author).info == "Ann; Bob");
  CHECK(row(result, Field::author).xmp.front().items == std::vector<std::string>{"Ann", "Bob"});
  CHECK(*row(result, Field::producer).info == "P2");
  CHECK(row(result, Field::producer).xmp.front().value == "P2");
  for (const MetadataReader::Row& each : result.rows) {
    CHECK(each.agree.value_or(true));
  }
  // The custom Info key stays as it was.
  auto document = open_document(written.pdf);
  const auto* info = document->info().dictionary();
  REQUIRE(info != nullptr);
  CHECK(info->get("Custom").string()->bytes == "Kept custom");

  const auto untouched = write(both().build(), R"({"Subject": "Only the subject"})");
  const MetadataReader::Result kept = reread(untouched.pdf);
  CHECK(*row(kept, Field::title).info == "Old title");
  CHECK(row(kept, Field::title).xmp.front().value == "Old title");
  CHECK(*row(kept, Field::subject).info == "Only the subject");
  // dc:description was absent, so it is added in a description of its own.
  REQUIRE_FALSE(row(kept, Field::subject).xmp.empty());
  CHECK(row(kept, Field::subject).xmp.front().value == "Only the subject");
}

TEST_CASE("custom namespaces survive byte for byte; the history goes; identifiers regenerate") {
  const auto first = write(both().build(), R"({"Title": "T"})");
  const std::string packet = packet_of(first.pdf);
  CHECK(packet.find(custom) != std::string::npos);
  CHECK(packet.find("History") == std::string::npos);
  CHECK(packet.find("saved by") == std::string::npos);
  CHECK(packet.find("uuid:old") == std::string::npos);
  const std::size_t did = packet.find("<xmpMM:DocumentID>xmp.did:");
  REQUIRE(did != std::string::npos);
  const std::size_t iid = packet.find("xmpMM:InstanceID='xmp.iid:");
  REQUIRE(iid != std::string::npos);
  const std::string document_id = packet.substr(did + 26, 32);
  const std::string instance_id = packet.substr(iid + 26, 32);
  CHECK(document_id != instance_id);
  CHECK(document_id.find_first_not_of("0123456789abcdef") == std::string::npos);
  // The trailer's /ID is the same 16 bytes, in upper case.
  std::string upper = document_id;
  for (char& c : upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  CHECK(first.pdf.find("<" + upper + ">") != std::string::npos);
  // Deterministic: the same plan gives the same bytes; another plan other
  // identifiers.
  CHECK(write(both().build(), R"({"Title": "T"})").pdf == first.pdf);
  CHECK(packet_of(write(both().build(), R"({"Title": "U"})").pdf).find(document_id) ==
        std::string::npos);
  // A write without metadata values regenerates them too.
  const std::string identity = pdfkit::test::identity_write(both().build());
  CHECK(packet_of(identity).find("xmp.did:") != std::string::npos);
  CHECK(packet_of(identity).find("uuid:old") == std::string::npos);
}

TEST_CASE("a value outside PDFDocEncoding is written as UTF-16BE with the mark") {
  const auto written = write(both().build(), R"({"Title": "Ω and €"})");
  auto document = open_document(written.pdf);
  const auto* info = document->info().dictionary();
  REQUIRE(info != nullptr);
  const std::string bytes = info->get("Title").string()->bytes;
  CHECK(bytes.starts_with("\xFE\xFF"));
  CHECK(*row(reread(written.pdf), Field::title).info == "\xCE\xA9 and \xE2\x82\xAC");
  const auto euro = write(both().build(), R"({"Title": "5 €"})");
  auto plain = open_document(euro.pdf);
  CHECK(plain->info().dictionary()->get("Title").string()->bytes == "5 \xA0");
  CHECK(written.set_answer.find("\"info_encoding\":\"utf16be\"") != std::string::npos);
}

TEST_CASE("a field set to null is removed from both places") {
  const auto written = write(both().build(), R"({"Author": null, "Producer": null})");
  const MetadataReader::Result result = reread(written.pdf);
  CHECK_FALSE(row(result, Field::author).info.has_value());
  CHECK(row(result, Field::author).xmp.empty());
  CHECK_FALSE(row(result, Field::producer).info.has_value());
  CHECK(row(result, Field::producer).xmp.empty());
  CHECK(pdfkit::test::everything(written.pdf).find("Old Maker") == std::string::npos);
  CHECK(written.set_answer.find("\"removed\":true") != std::string::npos);
}

TEST_CASE("dates are written as a PDF date and as ISO 8601") {
  const auto written =
      write(both().build(),
            R"({"CreationDate": "2023-04-05T06:07:08+02:00", "ModDate": "D:20240102030405Z"})");
  const MetadataReader::Result result = reread(written.pdf);
  CHECK(*row(result, Field::creation_date).info == "D:20230405060708+02'00'");
  CHECK(row(result, Field::creation_date).xmp.front().value == "2023-04-05T06:07:08+02:00");
  CHECK(*row(result, Field::mod_date).info == "D:20240102030405Z");
  CHECK(row(result, Field::mod_date).xmp.front().value == "2024-01-02T03:04:05Z");
  CHECK(require_value(row(result, Field::creation_date).agree));
  CHECK(require_value(row(result, Field::mod_date).agree));
}

TEST_CASE("an unreadable packet is dropped with its warning") {
  MetadataFixture fixture;
  fixture.info = "/Title (Old title)";
  fixture.packet = MetadataFixture::xmp("<dc:title>Old title</dc:creator>\n");
  const auto written = write(fixture.build(), R"({"Title": "New"})");
  CHECK(written.report.find("xmp_unreadable") != std::string::npos);
  const MetadataReader::Result result = reread(written.pdf);
  CHECK_FALSE(result.xmp_present);
  CHECK(*row(result, Field::title).info == "New");
  CHECK(written.set_answer.find("\"xmp\":null") != std::string::npos);
}

TEST_CASE("a file without an Info dictionary gains one; without a packet none is made") {
  MetadataFixture fixture;
  const auto written = write(fixture.build(), R"({"Title": "Made"})");
  const MetadataReader::Result result = reread(written.pdf);
  CHECK(result.info_present);
  CHECK(*row(result, Field::title).info == "Made");
  CHECK_FALSE(result.xmp_present);
}

TEST_CASE("a rewritten element keeps its start tag; an empty element gains its content") {
  MetadataFixture fixture;
  fixture.info = "/Author (Old) /Keywords (old)";
  // As pikepdf writes them: the namespace declared on the property element.
  fixture.packet =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description rdf:about=''>"
      "<dc:creator xmlns:dc='http://purl.org/dc/elements/1.1/'><rdf:Seq><rdf:li/></rdf:Seq>"
      "</dc:creator></rdf:Description><rdf:Description rdf:about='' "
      "xmlns:pdf='http://ns.adobe.com/pdf/1.3/'><pdf:Keywords /></rdf:Description>"
      "</rdf:RDF></x:xmpmeta>";
  const auto written = write(fixture.build(), R"({"Author": "New One", "Keywords": "k1"})");
  const MetadataReader::Result result = reread(written.pdf);
  REQUIRE_FALSE(row(result, Field::author).xmp.empty());
  CHECK(row(result, Field::author).xmp.front().value == "New One");
  CHECK(row(result, Field::keywords).xmp.front().value == "k1");
  CHECK(result.packet.find("<dc:creator xmlns:dc='http://purl.org/dc/elements/1.1/'>") !=
        std::string::npos);
  CHECK(result.packet.find("<pdf:Keywords>k1</pdf:Keywords>") != std::string::npos);
}

TEST_CASE("the toolkit attribute goes when it repeats a rewritten producer") {
  MetadataFixture fixture;
  fixture.info = "/Producer (Maker Toolkit 2.5)";
  fixture.packet =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/' x:xmptk='Maker Toolkit 2.5, http://maker.example'>"
      "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description "
      "rdf:about='' xmlns:pdf='http://ns.adobe.com/pdf/1.3/'><pdf:Producer>Maker Toolkit 2.5"
      "</pdf:Producer></rdf:Description></rdf:RDF></x:xmpmeta>";
  const XmpScanner::Result scanned = XmpScanner::scan(fixture.packet);
  REQUIRE(scanned.others.size() == 1);
  CHECK(scanned.others[0].local == "xmptk");
  const auto written = write(fixture.build(), R"({"Producer": "[_6_]"})");
  const std::string packet = reread(written.pdf).packet;
  CHECK(packet.find("xmptk") == std::string::npos);
  CHECK(packet.find("<x:xmpmeta xmlns:x='adobe:ns:meta/'>") != std::string::npos);
  CHECK(packet.find("<pdf:Producer>[_6_]</pdf:Producer>") != std::string::npos);
}

TEST_CASE("a packet that states nothing is dropped") {
  MetadataFixture fixture;
  fixture.info = "/Title (Old title)";
  fixture.packet = "\n";
  const auto written = write(fixture.build(), R"({"Title": "U"})");
  CHECK_FALSE(reread(written.pdf).xmp_present);
  CHECK(written.report.find("xmp_unreadable") == std::string::npos);
  CHECK(written.set_answer.find("\"xmp\":null") != std::string::npos);
  CHECK(written.set_answer.find("\"xmp_properties\":[]") != std::string::npos);
}

TEST_CASE("the preview names each field's written forms") {
  pdfkit::writer::MetadataWriter::Values values;
  values[Field::title] = "T";
  values[Field::creation_date] = "D:20200101";
  values[Field::author] = std::nullopt;
  const auto preview = pdfkit::writer::MetadataWriter::preview(values);
  REQUIRE(preview.size() == 3);
  CHECK(preview[0].field == Field::title);
  CHECK(preview[0].info_encoding == "pdf_doc");
  CHECK(preview[1].removed);
  CHECK(preview[2].info == "D:20200101");
  CHECK(preview[2].xmp == "2020-01-01T00:00:00");
}
