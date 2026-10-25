// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of redact::SelfCheck (docs/DESIGN.md section 18): a planted survivor
// in each surface (page text, hidden text, /ActualText, /Alt, /E, the Info
// dictionary, the XMP packet, a string object, a content stream's string
// operand, the raw bytes of another stream in UTF-16BE) is found and named
// by page and surface; a clean file passes; the placeholder font's own glyphs
// and the placeholders cut out of a surface are not survivors; the refusal
// carries the three parts; pdfkit_check answers over bytes.

#include "redact/self_check.hpp"

#include "pdfkit/pdfkit.h"

#include "platform/byte_span.hpp"
#include "redact/support/redaction.hpp"
#include "syntax/limits.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>
#include <vector>

namespace {

using pdfkit::redact::MatchFinder;
using pdfkit::redact::SelfCheck;

MatchFinder::Match needle(const std::string& text) {
  MatchFinder::Match match;
  match.search = text;
  match.placeholder = "[REDACTED]";
  return match;
}

SelfCheck::Result check(const std::string& file, const std::string& text,
                        const std::vector<std::string>& placeholders = {}) {
  const std::vector<MatchFinder::Match> needles{needle(text)};
  return SelfCheck::run(pdfkit::platform::as_bytes(file), pdfkit::syntax::Limits{}, needles,
                        placeholders);
}

bool found(const SelfCheck::Result& result, const std::string& surface,
           std::optional<std::uint32_t> page = std::nullopt) {
  for (const SelfCheck::Survivor& survivor : result.survivors) {
    if (survivor.surface == surface && (!page || survivor.page == page)) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("a clean file passes") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Nothing to see) Tj ET");
  const SelfCheck::Result result = check(fixture.bytes(), "Zebulon");
  CHECK(result.survivors.empty());
  CHECK(result.strings == 1);
  CHECK(result.pages == 1);
}

TEST_CASE("page text, hidden text and the content's string operands are surfaces") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Nothing) Tj ET");
  fixture.page("BT 3 Tr /F1 10 Tf 72 700 Td (zebulon) Tj ET");
  const SelfCheck::Result result = check(fixture.bytes(), "Zebulon");
  CHECK(found(result, "text", 2));
  CHECK_FALSE(found(result, "text", 1));
  CHECK(found(result, "stream", 2));
}

TEST_CASE("marked-content properties are surfaces") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "/Span << /ActualText (Zebulon) >> BDC EMC /Figure << /Alt (a Zebulon) >> BDC EMC "
      "/Span << /E (Zebulon) >> BDC BT /F1 10 Tf (x) Tj ET EMC");
  const SelfCheck::Result result = check(fixture.bytes(), "zebulon");
  CHECK(found(result, "actual_text", 1));
  CHECK(found(result, "alt", 1));
  CHECK(found(result, "expansion", 1));
}

TEST_CASE("the Info dictionary, the XMP packet and string objects are surfaces") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (x) Tj ET");
  const std::uint32_t xmp = fixture.add_stream(
      "/Type /Metadata /Subtype /XML",
      "<x:xmpmeta><rdf:RDF><rdf:Description dc:creator=\"Zebulon &amp; Co\"/></rdf:RDF>"
      "</x:xmpmeta>");
  const std::uint32_t other = fixture.add("<< /Note (by Zebulon) >>");
  fixture.info("<< /Title (Report on Zebulon) >>");
  fixture.catalog_entries("/Metadata " + std::to_string(xmp) + " 0 R /Extra " +
                          std::to_string(other) + " 0 R");
  const std::string file = fixture.bytes();
  CHECK(found(check(file, "Zebulon & Co"), "xmp"));
  CHECK(found(check(file, "report on zebulon"), "info"));
  const SelfCheck::Result strings = check(file, "by zebulon");
  CHECK(found(strings, "string"));
  CHECK(strings.survivors.front().object == other);
}

TEST_CASE("another stream's bytes are scanned in UTF-16BE") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (x) Tj ET");
  const std::string wide = std::string("\0Z\0e\0b\0u\0l\0o\0n", 14);
  const std::uint32_t blob = fixture.add_stream("/Type /Unknown", wide);
  fixture.catalog_entries("/Extra " + std::to_string(blob) + " 0 R");
  const SelfCheck::Result result = check(fixture.bytes(), "zebulon");
  CHECK(found(result, "stream"));
}

TEST_CASE("placeholders are cut out of every surface") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td ([REDACTED]) Tj ET /Span << /Alt ([REDACTED]) >> BDC EMC");
  CHECK_FALSE(check(fixture.bytes(), "RED", {"[REDACTED]"}).survivors.empty());
  // Drawn in a document font, the placeholder's text on the page is still
  // text; in a property it is cut.
  const SelfCheck::Result result = check(fixture.bytes(), "RED", {"[REDACTED]"});
  CHECK_FALSE(found(result, "alt"));
  CHECK_FALSE(found(result, "stream"));
}

TEST_CASE("a placeholder drawn by a redaction does not count as a survivor") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (The RED file) Tj ET");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("red"));
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(pdfkit::test::page_text(result.pdf).find("[REDACTED]") != std::string::npos);
}

TEST_CASE("a survivor refuses the write naming the page and the surface") {
  pdfkit::test::TextDocument fixture;
  fixture.page("/G1 gs BT /F1 10 Tf 72 700 Td (Zebulon) Tj ET",
               "/ExtGState << /G1 << /Type /ExtGState /Note (Zebulon was here) >> >>");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Zebulon"));
  CHECK(result.write_status == PDFKIT_E_SELF_CHECK);
  CHECK(result.pdf.empty());
  CHECK(result.report.find("\"code\":\"self_check\"") != std::string::npos);
  CHECK(result.report.find("string") != std::string::npos);
  CHECK(result.report.find("\"fix\"") != std::string::npos);
}

TEST_CASE("pdfkit_check answers every survivor by page and surface") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (A note on Zebulon) Tj ET");
  const std::string file = fixture.bytes();
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status =
      pdfkit_check(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(),
                   R"({"strings": [{"text": "zebulon"}, {"text": "absent"}]})", &out);
  const std::string answer(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  CHECK(status == PDFKIT_OK);
  CHECK(answer.find(R"({"string":"zebulon","page":1,"surface":"text","object":null})") !=
        std::string::npos);
  CHECK(answer.find("\"strings\":2") != std::string::npos);
  const pdfkit_status refused =
      pdfkit_check(reinterpret_cast<const std::uint8_t*>("junk"), 4, R"({"strings": []})", &out);
  pdfkit_buffer_free(&out);
  CHECK(refused == PDFKIT_E_REFUSED);
}
