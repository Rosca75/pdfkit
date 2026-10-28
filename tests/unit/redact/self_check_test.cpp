// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of redact::SelfCheck (docs/DESIGN.md section 18): a planted survivor
// in each surface (page text, hidden text, /ActualText, /Alt, /E, the Info
// dictionary, the XMP packet, a string object, a content stream's string
// operand, the raw bytes of another stream in UTF-16BE) is found and named
// by page and surface; a clean file passes; the placeholder font's own glyphs
// and the placeholders cut out of a surface are not survivors; the refusal
// carries the three parts; pdfkit_check answers over bytes. A glyph range is
// verified at its location only: a second occurrence elsewhere stays and the
// write passes, an array of ranges is one expression with one placeholder,
// the glyphs, the codes or a marked-content property left at the location
// are a survivor named "location", and a malformed array is refused before
// anything is recorded. A rewritten metadata field's original is searched in
// the metadata alone, whatever its length.

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

TEST_CASE("an XMP packet whose quotes do not balance is still searched") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (x) Tj ET");
  const std::uint32_t xmp = fixture.add_stream(
      "/Type /Metadata /Subtype /XML",
      "<?xpacket begin=\"\xEF\xBB? id=\"W5M0\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\" "
      "x:xmptk=\"Zebulon Core\"><rdf:Description/></x:xmpmeta>");
  fixture.catalog_entries("/Metadata " + std::to_string(xmp) + " 0 R");
  CHECK(found(check(fixture.bytes(), "zebulon core"), "xmp"));
  // Element and attribute names are markup, not text.
  CHECK_FALSE(found(check(fixture.bytes(), "Description"), "xmp"));
  CHECK_FALSE(found(check(fixture.bytes(), "xmptk"), "xmp"));
}

TEST_CASE("a comment of a content stream is a surface") {
  pdfkit::test::TextDocument fixture;
  fixture.page("% a note on Zebulon\nBT /F1 10 Tf 72 700 Td (x) Tj ET");
  CHECK(found(check(fixture.bytes(), "zebulon"), "stream", 1));
}

namespace {

// The fingerprint of file, from a probe redaction's answer.
std::string fingerprint_of(const std::string& file) {
  const pdfkit::test::Redaction probe = pdfkit::test::redact(file, pdfkit::test::search("x"));
  const std::size_t at = probe.matches.find(R"("fingerprint":")");
  REQUIRE(at != std::string::npos);
  return probe.matches.substr(at + 15, 64);
}

// The reading-order position where text starts on page 1 of file.
std::size_t position_of(const std::string& file, const std::string& text) {
  const pdfkit::text::PageText page = pdfkit::test::read_pages(file).front();
  std::string read;
  std::vector<std::size_t> position;
  for (std::size_t p = 0; p < page.order.size(); ++p) {
    const std::string& glyph = page.content.glyphs[page.order[p]].text;
    for (std::size_t i = 0; i < glyph.size(); ++i) {
      position.push_back(p);
    }
    read += glyph;
  }
  const std::size_t at = read.find(text);
  REQUIRE(at != std::string::npos);
  return position[at];
}

std::size_t count_of(const std::string& text, const std::string& word) {
  std::size_t count = 0;
  for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
    ++count;
  }
  return count;
}

std::string range(std::size_t first, std::size_t last, std::uint32_t page = 1) {
  return R"({"page": )" + std::to_string(page) + R"(, "first": )" + std::to_string(first) +
         R"(, "last": )" + std::to_string(last) + "}";
}

std::string located(const std::string& glyphs, const std::string& fingerprint) {
  return R"({"matches": [{"glyphs": )" + glyphs + R"(, "fingerprint": ")" + fingerprint + "\"}]}";
}

std::string take(pdfkit_buffer& buffer) {
  std::string out(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  return out;
}

// A rewrite of metadata fields through the C ABI: the write's status, report
// and output.
pdfkit::test::Redaction rewrite(const std::string& file, const std::string& fields) {
  pdfkit::test::Redaction result;
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                      &doc, &out) == PDFKIT_OK);
  (void)take(out);
  pdfkit_edit* edit = nullptr;
  REQUIRE(pdfkit_edit_new(doc, &edit, &out) == PDFKIT_OK);
  (void)take(out);
  result.match_status = pdfkit_set_metadata(edit, fields.c_str(), &out);
  result.matches = take(out);
  pdfkit_buffer pdf{nullptr, 0, nullptr};
  result.write_status = pdfkit_write(edit, nullptr, &pdf, &out);
  result.report = take(out);
  result.pdf = take(pdf);
  pdfkit_edit_free(edit);
  pdfkit_close(doc);
  return result;
}

SelfCheck::Result check_located(const std::string& file, const SelfCheck::Located& entry) {
  const std::vector<SelfCheck::Located> entries{entry};
  return SelfCheck::run(pdfkit::platform::as_bytes(file), pdfkit::syntax::Limits{},
                        SelfCheck::Needles{{}, entries, {}}, std::vector<std::string>{});
}

}  // namespace

TEST_CASE("a glyph range is verified at its location: the other occurrences stay") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (The EU and the EU) Tj 0 -14 Td (EU) Tj ET");
  const std::string file = fixture.bytes();
  const std::size_t first = position_of(file, "EU");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, located(range(first, first + 2), fingerprint_of(file)));
  REQUIRE(result.match_status == PDFKIT_OK);
  INFO(result.report);
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(count_of(pdfkit::test::page_text(file), "EU") == 3);
  CHECK(count_of(pdfkit::test::page_text(result.pdf), "EU") == 2);
  CHECK(result.report.find(R"("self_check":{"status":"passed","strings":1,"located":1,)") !=
        std::string::npos);
}

TEST_CASE("an array of ranges is one expression with one placeholder") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Jane M Doe met Jane Doe) Tj ET");
  const std::string file = fixture.bytes();
  const std::size_t jane = position_of(file, "Jane");
  const std::size_t doe = position_of(file, "Doe");
  // Given out of order: the answer holds them sorted.
  const pdfkit::test::Redaction result = pdfkit::test::redact(
      file, located("[" + range(doe, doe + 3) + ", " + range(jane, jane + 4) + "]",
                    fingerprint_of(file)));
  REQUIRE(result.match_status == PDFKIT_OK);
  INFO(result.matches);
  INFO(result.report);
  CHECK(result.matches.find(R"("count":1)") != std::string::npos);
  CHECK(result.matches.find("\"glyphs\":[[" + std::to_string(jane) + "," +
                            std::to_string(jane + 4) + "],[" + std::to_string(doe) + "," +
                            std::to_string(doe + 3) + "]]") != std::string::npos);
  CHECK(result.matches.find(R"("text":"Jane Doe")") != std::string::npos);
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(result.report.find(R"("placeholders":1)") != std::string::npos);
  const std::string text = pdfkit::test::page_text(result.pdf);
  CHECK(count_of(text, "[REDACTED]") == 1);
  CHECK(text.find("Jane Doe") != std::string::npos);
  CHECK(text.find(" M ") != std::string::npos);
}

TEST_CASE("a located match whose glyphs survive is refused at its location") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (The EU and the EU) Tj ET");
  const std::string file = fixture.bytes();
  // The surgery's fault: the output is the input itself.
  const pdfkit::text::PageText page = pdfkit::test::read_pages(file).front();
  const std::size_t first = position_of(file, "EU");
  SelfCheck::Located entry;
  entry.text = "EU";
  entry.page = 1;
  for (std::size_t p = first; p < first + 2; ++p) {
    entry.boxes.push_back(page.content.glyphs[page.order[p]].box);
  }
  entry.codes = {"4555"};
  const SelfCheck::Result result = check_located(file, entry);
  REQUIRE(result.survivors.size() == 1);
  CHECK(result.survivors.front().surface == "location");
  CHECK(result.survivors.front().page == 1);
  CHECK(result.located == 1);
  // The codes alone, where the text does not read the expression, survive
  // too.
  entry.text = "Zebulon";
  CHECK(found(check_located(file, entry), "location", 1));
  // Elsewhere on the page the text is not searched for.
  entry.text = "EU";
  entry.boxes = {pdfkit::text::Rect::of(300, 300, 320, 320)};
  CHECK(check_located(file, entry).survivors.empty());
}

TEST_CASE("a marked-content property over the location holding the text survives there") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td /Span << /Alt (the EU) >> BDC (xy) Tj EMC ET");
  const std::string file = fixture.bytes();
  const pdfkit::text::PageText page = pdfkit::test::read_pages(file).front();
  SelfCheck::Located entry;
  entry.text = "EU";
  entry.page = 1;
  entry.boxes = {page.content.glyphs.front().box};
  CHECK(found(check_located(file, entry), "location", 1));
}

TEST_CASE("a rewritten metadata field is verified in the metadata, whatever its length") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Bought a PC today) Tj ET");
  fixture.info("<< /Author (PC) /Title (Notes) >>");
  const pdfkit::test::Redaction written = rewrite(fixture.bytes(), R"({"Author": "Someone"})");
  INFO(written.report);
  REQUIRE(written.write_status == PDFKIT_OK);
  CHECK(written.report.find(R"("metadata_values":1)") != std::string::npos);
  CHECK(pdfkit::test::page_text(written.pdf).find("PC") != std::string::npos);

  // The same value left in a field the caller did not rewrite is refused (a
  // key outside the field list that repeats it is dropped by the writer).
  pdfkit::test::TextDocument kept;
  kept.page("BT /F1 10 Tf 72 700 Td (Bought a PC today) Tj ET");
  kept.info("<< /Author (PC) /Subject (by PC) >>");
  const pdfkit::test::Redaction refused = rewrite(kept.bytes(), R"({"Author": "Someone"})");
  CHECK(refused.write_status == PDFKIT_E_SELF_CHECK);
  CHECK(refused.report.find("in the info") != std::string::npos);
}

TEST_CASE("an array of ranges on two pages, overlapping or empty, is refused before recording") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (The EU and the EU) Tj ET");
  fixture.page("BT /F1 10 Tf 72 700 Td (More EU) Tj ET");
  const std::string file = fixture.bytes();
  const std::string fingerprint = fingerprint_of(file);
  for (const std::string& glyphs :
       {std::string("[]"), "[" + range(0, 3) + ", " + range(0, 2, 2) + "]",
        "[" + range(0, 3) + ", " + range(2, 5) + "]", "[" + range(2, 2) + "]"}) {
    INFO(glyphs);
    const pdfkit::test::Redaction result = pdfkit::test::redact(file, located(glyphs, fingerprint));
    CHECK(result.match_status == PDFKIT_E_INVALID_ARGUMENT);
    CHECK(result.matches.find("\"code\":\"invalid_argument\"") != std::string::npos);
  }
}
