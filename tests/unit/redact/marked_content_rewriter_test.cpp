// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Marked-content properties do not keep a redacted string (docs/DESIGN.md
// section 12): an /ActualText inline and through /Properties, an /Alt and an
// /E; a span covering only the match loses its /ActualText with its glyphs;
// a value naming the string beside other text keeps the rest; the rewriter's
// pieces (the closing EMC, the replacement, the text string) on their own.

#include "redact/marked_content_rewriter.hpp"

#include "redact/support/redaction.hpp"
#include "text/support/text_document.hpp"
#include "text/text_string.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::redact::MarkedContentRewriter;
using pdfkit::redact::MatchFinder;

MatchFinder::Match needle(const std::string& text, const std::string& placeholder) {
  MatchFinder::Match match;
  match.search = text;
  match.placeholder = placeholder;
  return match;
}

}  // namespace

TEST_CASE("an inline ActualText over the match goes with its glyphs") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "BT /F1 10 Tf 72 700 Td (Dear ) Tj /Span << /ActualText (Zebulon) >> BDC (Zeb) Tj EMC "
      "( and others) Tj ET");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.report);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "(Zeb)"));
  const std::string text = pdfkit::test::page_text(result.pdf);
  CHECK(text.find("[REDACTED]") != std::string::npos);
  CHECK(text.find("others") != std::string::npos);
}

TEST_CASE("an ActualText through Properties is rewritten in the resource") {
  pdfkit::test::TextDocument fixture;
  const std::uint32_t properties = fixture.add("<< /ActualText (Zebulon Quarterfield) >>");
  fixture.page("BT /F1 10 Tf 72 700 Td /Span /P1 BDC (ZQ) Tj EMC ( rest) Tj ET",
               "/Properties << /P1 " + std::to_string(properties) + " 0 R >>");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Quarterfield"));
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.report);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Quarterfield"));
  CHECK(pdfkit::test::page_text(result.pdf).find("rest") != std::string::npos);
}

TEST_CASE("an Alt and an E naming the string are rewritten where the glyphs stay") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "BT /F1 10 Tf 72 700 Td /Figure << /Alt (Photo of Zebulon Quarterfield) >> BDC (Photo) Tj "
      "EMC /Span << /E (Mister Quarterfield) >> BDC ( Mr.) Tj EMC ET");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Quarterfield"));
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Quarterfield"));
  CHECK(pdfkit::test::contains_marker(result.pdf, "Photo of Zebulon [REDACTED]"));
  CHECK(pdfkit::test::contains_marker(result.pdf, "Mister [REDACTED]"));
  CHECK(pdfkit::test::page_text(result.pdf) == "Photo Mr.");
}

TEST_CASE("the closing EMC is found across nesting") {
  pdfkit::test::TextDocument fixture;
  fixture.page("/A BMC /B << /Alt (x) >> BDC /C BMC EMC EMC EMC (unclosed) Tj /D BMC");
  const pdfkit::text::PageContent content = fixture.interpret();
  const pdfkit::text::ContentStream& stream = content.streams[0];
  CHECK(MarkedContentRewriter::closing(stream, 0) == 5);
  CHECK(MarkedContentRewriter::closing(stream, 1) == 4);
  CHECK(MarkedContentRewriter::closing(stream, 7) == stream.operations.size());
}

TEST_CASE("a value has each needle replaced and keeps the rest") {
  const std::vector<MatchFinder::Match> needles{needle("jane doe", "[A]"), needle("acme", "[B]")};
  CHECK(MarkedContentRewriter::replace("Letter from JANE  DOE of Acme Ltd", needles) ==
        "Letter from [A] of [B] Ltd");
  CHECK_FALSE(MarkedContentRewriter::replace("nothing here", needles).has_value());
}

TEST_CASE("a text string is ASCII when it can be and UTF-16BE otherwise") {
  const pdfkit::syntax::Object ascii = MarkedContentRewriter::text_string("[REDACTED] text");
  REQUIRE(ascii.string() != nullptr);
  CHECK(ascii.string()->bytes == "[REDACTED] text");
  const pdfkit::syntax::Object wide = MarkedContentRewriter::text_string("\xC3\x89lise");
  REQUIRE(wide.string() != nullptr);
  CHECK(wide.string()->bytes.substr(0, 2) == "\xFE\xFF");
  CHECK(pdfkit::text::TextString::to_utf8(wide.string()->bytes) == "\xC3\x89lise");
}
