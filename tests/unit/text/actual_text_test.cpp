// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of text::ActualText, the fourth rung of the decoding ladder: a
// ligature glyph under /ActualText reads as its letters with the rung
// actual_text; a span whose /ActualText agrees with its glyphs is left as
// decoded; in nested spans the outer one is applied; the replacement text
// goes to the span's first glyph and the others are covered by it.

#include "text/actual_text.hpp"

#include "fonts/decoding_rung.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::fonts::DecodingRung;
using pdfkit::test::TextDocument;
using pdfkit::text::PageText;

PageText read(const std::string& content) {
  TextDocument document;
  document.page(content);
  return document.read();
}

}  // namespace

TEST_CASE("a glyph under /ActualText reads as the span's text") {
  // WinAnsi code 0x95 is the bullet; the span says it is "fi".
  const PageText page =
      read("BT /F1 10 Tf 72 700 Td /Span << /ActualText (fi) >> BDC (\\225) Tj EMC (nd) Tj ET");
  REQUIRE(page.content.glyphs.size() == 3U);
  CHECK(page.content.glyphs[0].text == "fi");
  CHECK(page.content.glyphs[0].rung == DecodingRung::actual_text);
  CHECK(page.content.glyphs[1].rung == DecodingRung::glyph_name);
  CHECK(page.text() == "find");
  REQUIRE(page.content.spans.size() == 1U);
  CHECK(page.content.spans[0].actual_text == "fi");
}

TEST_CASE("a span whose /ActualText agrees is left as decoded") {
  const PageText page =
      read("/Span << /ActualText (AB) >> BDC BT /F1 10 Tf 72 700 Td (AB) Tj ET EMC");
  REQUIRE(page.content.glyphs.size() == 2U);
  CHECK(page.content.glyphs[0].rung == DecodingRung::glyph_name);
  CHECK(page.content.glyphs[1].text == "B");
  CHECK_FALSE(page.content.glyphs[1].actual_text_covered);
}

TEST_CASE("the outer span of nested /ActualText spans is applied") {
  const PageText page = read(
      "/Span << /ActualText <FEFF00480065006C006C006F> >> BDC BT /F1 10 Tf 72 700 Td "
      "/Span << /ActualText (He) >> BDC (Hx) Tj EMC (lo) Tj ET EMC");
  REQUIRE(page.content.glyphs.size() == 4U);
  CHECK(page.content.glyphs[0].text == "Hello");
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(page.content.glyphs[i].rung == DecodingRung::actual_text);
    CHECK(page.content.glyphs[i].actual_text == 0U);
  }
  CHECK(page.content.glyphs[1].text.empty());
  CHECK(page.content.glyphs[3].actual_text_covered);
  CHECK(page.text() == "Hello");
  REQUIRE(page.content.spans.size() == 2U);
  CHECK(page.content.spans[1].parent == 0U);
}
