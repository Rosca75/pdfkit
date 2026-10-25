// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::MetadataReader and syntax::PdfDocEncoding: each Info field
// in PDFDocEncoding and in UTF-16BE, a UTF-16LE value read and counted, each
// XMP field in element and attribute form, the three arrays, agreeing and
// disagreeing rows, a file with Info only and one with XMP only, an
// encrypted file's strings decrypted and a packet left clear by
// /EncryptMetadata false, the date normalisation, and the PDFDoc table
// checked code by code against the text layer's reading through the fonts
// layer's Annex D names.

#include "syntax/metadata_reader.hpp"

#include "metadata/support/metadata_fixture.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "support/require_value.hpp"
#include "syntax/pdf_doc_encoding.hpp"
#include "text/text_string.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::syntax::MetadataReader;
using pdfkit::syntax::PdfDocEncoding;
using pdfkit::test::MetadataFixture;
using pdfkit::test::open_document;
using pdfkit::test::PdfBuilder;
using pdfkit::test::require_value;
using Field = MetadataReader::Field;

MetadataReader::Result read(const MetadataFixture& fixture) {
  auto document = open_document(fixture.build());
  return MetadataReader::read(*document);
}

const MetadataReader::Row& row(const MetadataReader::Result& result, Field field) {
  for (const MetadataReader::Row& candidate : result.rows) {
    if (candidate.field == field) {
      return candidate;
    }
  }
  FAIL("no row for the field");
  return result.rows.front();
}

const std::string every_field_xmp = MetadataFixture::xmp(
    "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">A title</rdf:li></rdf:Alt></dc:title>\n"
    "<dc:creator><rdf:Seq><rdf:li>Ann Author</rdf:li></rdf:Seq></dc:creator>\n"
    "<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">A subject</rdf:li></rdf:Alt>"
    "</dc:description>\n"
    "<dc:subject><rdf:Bag><rdf:li>one</rdf:li><rdf:li>two</rdf:li></rdf:Bag></dc:subject>\n"
    "<pdf:Keywords>one, two</pdf:Keywords>\n"
    "<xmp:CreatorTool>Writer</xmp:CreatorTool>\n"
    "<xmp:CreateDate>2020-01-02T03:04:05+01:00</xmp:CreateDate>\n",
    "pdf:Producer=\"Maker\" xmp:ModifyDate=\"2021-06-07T08:09:10Z\"");

const std::string every_field_info =
    "/Title (A title) /Author (Ann Author) /Subject (A subject) /Keywords (one, two) "
    "/Creator (Writer) /Producer (Maker) /CreationDate (D:20200102030405+01'00') "
    "/ModDate (D:20210607080910Z)";

}  // namespace

TEST_CASE("every field read from both places agrees") {
  MetadataFixture fixture;
  fixture.info = every_field_info;
  fixture.packet = every_field_xmp;
  const MetadataReader::Result result = read(fixture);
  CHECK(result.info_present);
  CHECK(result.xmp_present);
  CHECK(result.xmp_readable);
  REQUIRE(result.rows.size() == 8);
  for (const MetadataReader::Row& each : result.rows) {
    INFO(MetadataReader::name(each.field));
    REQUIRE(each.info.has_value());
    REQUIRE_FALSE(each.xmp.empty());
    CHECK(require_value(each.agree));
  }
  CHECK(*row(result, Field::title).info == "A title");
  CHECK(row(result, Field::title).xmp.front().property == "dc:title");
  CHECK(row(result, Field::author).xmp.front().value == "Ann Author");
  CHECK(row(result, Field::producer).xmp.front().value == "Maker");
  const MetadataReader::Row& keywords = row(result, Field::keywords);
  REQUIRE(keywords.xmp.size() == 2);
  CHECK(keywords.xmp[0].property == "pdf:Keywords");
  CHECK(keywords.xmp[1].property == "dc:subject");
  CHECK(keywords.xmp[1].value == "one; two");
  const MetadataReader::Row& created = row(result, Field::creation_date);
  CHECK(*created.info == "D:20200102030405+01'00'");
  CHECK(*created.info_iso == "2020-01-02T03:04:05+01:00");
  CHECK(*created.xmp_iso == "2020-01-02T03:04:05+01:00");
  CHECK(*row(result, Field::mod_date).info_iso == "2021-06-07T08:09:10Z");
}

TEST_CASE("disagreeing rows are reported as such") {
  MetadataFixture fixture;
  fixture.info = "/Title (Old title) /Producer (Maker) /ModDate (D:2021)";
  fixture.packet = MetadataFixture::xmp(
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>New title</rdf:li></rdf:Alt></dc:title>\n"
      "<pdf:Producer>Maker</pdf:Producer>\n<xmp:ModifyDate>2022-01-01</xmp:ModifyDate>\n");
  const MetadataReader::Result result = read(fixture);
  CHECK_FALSE(require_value(row(result, Field::title).agree));
  CHECK(require_value(row(result, Field::producer).agree));
  CHECK_FALSE(require_value(row(result, Field::mod_date).agree));
  CHECK_FALSE(row(result, Field::author).agree.has_value());
}

TEST_CASE("Info strings in UTF-16BE, UTF-16LE and PDFDocEncoding") {
  MetadataFixture fixture;
  // "Zoë" in UTF-16BE, "Ωmega" in UTF-16LE, a PDFDoc bullet and Euro.
  fixture.info =
      "/Title <FEFF005A006F00EB> /Author <FFFEA9036D0065006700610000>"
      " /Subject <80A0>";
  const MetadataReader::Result result = read(fixture);
  CHECK(result.info_present);
  CHECK_FALSE(result.xmp_present);
  CHECK(*row(result, Field::title).info == "Zo\xC3\xAB");
  CHECK(row(result, Field::title).info_form == PdfDocEncoding::Form::utf16be);
  CHECK(row(result, Field::author).info_form == PdfDocEncoding::Form::utf16le);
  CHECK(*row(result, Field::author).info == "\xCE\xA9mega");
  CHECK(*row(result, Field::subject).info == "\xE2\x80\xA2\xE2\x82\xAC");
  CHECK(result.utf16le == 1);
  CHECK_FALSE(row(result, Field::title).agree.has_value());
}

TEST_CASE("a file with XMP only, and one with neither") {
  MetadataFixture fixture;
  fixture.packet = every_field_xmp;
  const MetadataReader::Result result = read(fixture);
  CHECK_FALSE(result.info_present);
  CHECK(result.xmp_readable);
  CHECK(row(result, Field::creator).xmp.front().value == "Writer");
  CHECK_FALSE(row(result, Field::creator).info.has_value());

  const MetadataReader::Result none = read(MetadataFixture{});
  CHECK_FALSE(none.info_present);
  CHECK_FALSE(none.xmp_present);
  CHECK(none.rows.size() == 8);
}

TEST_CASE("an unreadable packet is reported and warned, never guessed at") {
  MetadataFixture fixture;
  fixture.info = "/Title (A title)";
  fixture.packet = MetadataFixture::xmp("<dc:title>Broken</dc:creator>\n");
  auto document = open_document(fixture.build());
  const MetadataReader::Result result = MetadataReader::read(*document);
  CHECK(result.xmp_present);
  CHECK_FALSE(result.xmp_readable);
  CHECK(result.xmp_fault.find("closes") != std::string::npos);
  CHECK(row(result, Field::title).xmp.empty());
  bool warned = false;
  for (const auto& warning : document->warnings()) {
    warned = warned || warning.code == "xmp_unreadable";
  }
  CHECK(warned);
}

TEST_CASE("an encrypted file's Info strings decrypt; /EncryptMetadata false leaves XMP clear") {
  for (const bool encrypt_metadata : {true, false}) {
    MetadataFixture fixture;
    fixture.info = "/Title " + PdfBuilder::str("Secret title");
    fixture.packet = every_field_xmp;
    pdfkit::test::PdfEncryptor::Spec spec;
    spec.revision = 4;
    spec.encrypt_metadata = encrypt_metadata;
    fixture.encryption = spec;
    const std::string file = fixture.build();
    CHECK(file.find("Secret title") == std::string::npos);
    CHECK((file.find("A title") != std::string::npos) == !encrypt_metadata);
    auto document = open_document(file);
    const MetadataReader::Result result = MetadataReader::read(*document);
    CHECK(*row(result, Field::title).info == "Secret title");
    CHECK(result.xmp_readable);
    CHECK(row(result, Field::title).xmp.front().value == "A title");
  }
}

TEST_CASE("dates normalise both ways") {
  CHECK(*MetadataReader::pdf_date_to_iso("D:2020") == "2020-01-01T00:00:00");
  CHECK(*MetadataReader::pdf_date_to_iso("D:20200102030405-05'30'") == "2020-01-02T03:04:05-05:30");
  CHECK(*MetadataReader::pdf_date_to_iso("20200102030405+00'00'") == "2020-01-02T03:04:05Z");
  CHECK(*MetadataReader::pdf_date_to_iso("D:20200102030405Z00'00'") == "2020-01-02T03:04:05Z");
  CHECK_FALSE(MetadataReader::pdf_date_to_iso("yesterday").has_value());
  CHECK_FALSE(MetadataReader::pdf_date_to_iso("D:20201302").has_value());
  CHECK(*MetadataReader::iso_date("2020-01-02T03:04:05.123+02:00") == "2020-01-02T03:04:05+02:00");
  CHECK(*MetadataReader::iso_date("2020-01") == "2020-01-01T00:00:00");
  CHECK(*MetadataReader::iso_date("2020-01-02T03:04Z") == "2020-01-02T03:04:00Z");
  CHECK_FALSE(MetadataReader::iso_date("2020/01/02").has_value());
  CHECK(*MetadataReader::iso_to_pdf_date("2020-01-02T03:04:05+02:00") == "D:20200102030405+02'00'");
  CHECK(*MetadataReader::iso_to_pdf_date("2020-01-02T03:04:05Z") == "D:20200102030405Z");
}

TEST_CASE("the PDFDoc table reads every code as the text layer reads it") {
  for (unsigned code = 0; code < 256; ++code) {
    const std::string byte(1, static_cast<char>(code));
    INFO("code " << code);
    CHECK(PdfDocEncoding::decode(byte).text == pdfkit::text::TextString::to_utf8(byte));
  }
}

TEST_CASE("text strings are written in PDFDocEncoding when they can be") {
  CHECK(PdfDocEncoding::text_string("Plain (text)") == "Plain (text)");
  CHECK(PdfDocEncoding::text_string("\xE2\x82\xAC 5") == "\xA0 5");
  CHECK(PdfDocEncoding::text_string("\xCE\xA9") == std::string("\xFE\xFF\x03\xA9", 4));
  CHECK(PdfDocEncoding::utf16be("\xF0\x9F\x98\x80") == std::string("\xFE\xFF\xD8\x3D\xDE\x00", 6));
  // A PDFDoc string that would start with a byte-order mark is written as
  // UTF-16BE.
  CHECK(PdfDocEncoding::text_string("\xC3\xBE\xC3\xBF").starts_with("\xFE\xFF\x00\xFE"));
  for (const std::string text : {"Zo\xC3\xAB", "\xCE\xA9mega", "\xE2\x80\xA2 x", "plain"}) {
    CHECK(PdfDocEncoding::decode(PdfDocEncoding::text_string(text)).text == text);
  }
}
