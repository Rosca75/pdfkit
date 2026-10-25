// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of text::ContentInterpreter: each operator's effect on the next
// glyph's origin and advance (Td, TD, Tm, T*, ', ", Tc, Tw on a one-byte
// space only, TJ numbers, Tz, Ts, cm), vertical writing, forms drawn with
// their matrices and walked once per draw, the self-drawing form and the
// depth limit, image occurrences numbered through forms, figures grouped
// between q and Q, clipping, marked content and optional content. Helvetica's
// widths are the Core 14 AFM's: H 722, i 222, space 278, A 667, B 667.

#include "text/content_interpreter.hpp"

#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>

namespace {

using doctest::Approx;
using pdfkit::test::TextDocument;
using pdfkit::text::GlyphRecord;
using pdfkit::text::ImageOccurrence;
using pdfkit::text::PageContent;

PageContent one_page(const std::string& content, const std::string& resources = "") {
  TextDocument document;
  document.page(content, resources);
  return document.interpret();
}

}  // namespace

TEST_CASE("a Tj places each glyph at the advance of the one before") {
  const PageContent page = one_page("BT /F1 12 Tf 72 700 Td (Hi) Tj ET");
  REQUIRE(page.glyphs.size() == 2U);
  const GlyphRecord& h = page.glyphs[0];
  const GlyphRecord& i = page.glyphs[1];
  CHECK(h.text == "H");
  CHECK(h.rung == pdfkit::fonts::DecodingRung::glyph_name);
  CHECK(h.origin.x == Approx(72.0));
  CHECK(h.origin.y == Approx(700.0));
  CHECK(h.advance == Approx(722.0 * 12.0 / 1000.0));
  CHECK(i.origin.x == Approx(72.0 + 8.664));
  CHECK(h.box.left == Approx(72.0));
  CHECK(h.box.right == Approx(80.664));
  CHECK(h.box.bottom < 700.0);
  CHECK(h.box.top > 700.0);
  CHECK(h.size == Approx(12.0));
  CHECK(h.angle == Approx(0.0));
  CHECK(h.operation == 3U);
  CHECK(h.draw == 0U);
  CHECK(i.draw == 1U);
}

TEST_CASE("Tc spaces every glyph and Tw only the one-byte space") {
  const PageContent page = one_page("BT /F1 10 Tf 2 Tc 5 Tw 0 0 Td (A B) Tj ET");
  REQUIRE(page.glyphs.size() == 3U);
  CHECK(page.glyphs[0].advance == Approx(6.67 + 2.0));
  CHECK(page.glyphs[1].advance == Approx(2.78 + 2.0 + 5.0));
  CHECK(page.glyphs[2].origin.x == Approx(6.67 + 2.0 + 2.78 + 7.0));
}

TEST_CASE("Tw never applies to a two-byte code, even code 32") {
  const PageContent page = one_page("BT /F2 10 Tf 5 Tw 0 0 Td <00200041> Tj ET");
  REQUIRE(page.glyphs.size() == 2U);
  CHECK(page.glyphs[0].code == 0x20U);
  CHECK(page.glyphs[0].code_length == 2U);
  CHECK(page.glyphs[0].advance == Approx(2.5));
  CHECK(page.glyphs[1].origin.x == Approx(2.5));
  CHECK(page.glyphs[1].text == "A");
}

TEST_CASE("a TJ number moves the next glyph by -n/1000 of the size") {
  const PageContent page = one_page("BT /F1 10 Tf 0 0 Td [(A) -1000 (B) 500 (A)] TJ ET");
  REQUIRE(page.glyphs.size() == 3U);
  CHECK(page.glyphs[1].origin.x == Approx(6.67 + 10.0));
  CHECK(page.glyphs[1].adjustment == Approx(-1000.0));
  CHECK(page.glyphs[2].origin.x == Approx(6.67 + 10.0 + 6.67 - 5.0));
  CHECK(page.glyphs[0].operand == 0U);
  CHECK(page.glyphs[1].operand == 2U);
  CHECK(page.glyphs[2].operand == 4U);
}

TEST_CASE("Tz scales the advance and Ts raises the origin") {
  const PageContent page = one_page("BT /F1 10 Tf 50 Tz 3 Ts 0 0 Td (AB) Tj ET");
  REQUIRE(page.glyphs.size() == 2U);
  CHECK(page.glyphs[0].advance == Approx(3.335));
  CHECK(page.glyphs[1].origin.x == Approx(3.335));
  CHECK(page.glyphs[0].origin.y == Approx(3.0));
  CHECK(page.glyphs[0].rise == Approx(3.0));
}

TEST_CASE("TD sets the leading that T*, ' and \" then use") {
  const PageContent page = one_page(
      "BT /F1 10 Tf 100 700 Td (A) Tj 0 -14 TD (B) Tj T* (A) Tj (B) ' 3 1 (A) \" ET");
  REQUIRE(page.glyphs.size() == 5U);
  CHECK(page.glyphs[1].origin.y == Approx(686.0));
  CHECK(page.glyphs[2].origin.y == Approx(672.0));
  CHECK(page.glyphs[3].origin.y == Approx(658.0));
  CHECK(page.glyphs[4].origin.y == Approx(644.0));
  CHECK(page.glyphs[4].origin.x == Approx(100.0));
  CHECK(page.glyphs[4].word_spacing == Approx(3.0));
  CHECK(page.glyphs[4].char_spacing == Approx(1.0));
  CHECK(page.glyphs[4].operand == 2U);
}

TEST_CASE("Tm and cm place text in device space, rotation included") {
  const PageContent page = one_page("q 0 1 -1 0 300 100 cm BT /F1 10 Tf 1 0 0 1 20 0 Tm (A) Tj ET Q");
  REQUIRE(page.glyphs.size() == 1U);
  CHECK(page.glyphs[0].origin.x == Approx(300.0));
  CHECK(page.glyphs[0].origin.y == Approx(120.0));
  CHECK(page.glyphs[0].angle == Approx(90.0));
  CHECK(page.glyphs[0].advance_end.y == Approx(126.67));
}

TEST_CASE("vertical writing advances down the page by the vertical width") {
  const PageContent page = one_page("BT /F3 10 Tf 300 700 Td <4E2D4E2D> Tj ET");
  REQUIRE(page.glyphs.size() == 2U);
  CHECK(page.glyphs[0].vertical);
  CHECK(page.glyphs[0].origin.y == Approx(700.0));
  CHECK(page.glyphs[1].origin.y == Approx(690.0));
  CHECK(page.glyphs[0].angle == Approx(-90.0));
  CHECK(page.glyphs[0].box.top == Approx(700.0));
  CHECK(page.glyphs[0].box.bottom == Approx(690.0));
}

TEST_CASE("a form is walked with its matrix once per draw and carries its path") {
  TextDocument document;
  const std::uint32_t form = document.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 200 50] /Matrix [1 0 0 1 10 0]",
      "BT /F1 10 Tf 0 0 Td (A) Tj ET");
  document.page("q 1 0 0 1 100 100 cm /Fm Do Q q 1 0 0 1 100 300 cm /Fm Do Q",
                "/XObject << /Fm " + std::to_string(form) + " 0 R >>");
  const PageContent page = document.interpret();
  REQUIRE(page.glyphs.size() == 2U);
  REQUIRE(page.streams.size() == 3U);
  CHECK(page.glyphs[0].origin.x == Approx(110.0));
  CHECK(page.glyphs[0].origin.y == Approx(100.0));
  CHECK(page.glyphs[1].origin.y == Approx(300.0));
  CHECK(page.glyphs[0].stream == 1U);
  CHECK(page.glyphs[1].stream == 2U);
  CHECK(page.streams[1].form == form);
  CHECK(page.streams[1].form_path == std::vector<std::uint32_t>{form});
  CHECK(page.glyphs[0].clip.right == Approx(310.0));
}

TEST_CASE("a form that draws itself is refused for that draw and the walk ends") {
  TextDocument document;
  // The form names itself: objects 1 to 5 are the catalog, the page tree and
  // the three fonts, so the form is object 6.
  const std::uint32_t loop = document.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 200 50] /Resources << /XObject << /Me 6 0 R >> "
      "/Font << /F1 3 0 R >> >>",
      "BT /F1 10 Tf 0 0 Td (A) Tj ET /Me Do");
  REQUIRE(loop == 6U);
  document.page("/Fm Do", "/XObject << /Fm 6 0 R >>");
  const PageContent page = document.interpret();
  CHECK(page.glyphs.size() == 1U);
  REQUIRE(!page.faults.empty());
  CHECK(page.faults.front().find("draws itself") != std::string::npos);
}

TEST_CASE("forms nested past the depth limit are skipped with a note") {
  TextDocument document;
  // Forms 5 to 40 each draw the next; the deepest shows a glyph.
  std::uint32_t next = 0;
  for (int i = 39; i >= 0; --i) {
    const std::string body = next == 0 ? "BT /F1 10 Tf (A) Tj ET" : "/N Do";
    const std::string resources =
        next == 0 ? "/Font << /F1 3 0 R >>"
                  : "/XObject << /N " + std::to_string(next) + " 0 R >>";
    next = document.add_stream(
        "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << " + resources + " >>", body);
  }
  document.page("/N Do", "/XObject << /N " + std::to_string(next) + " 0 R >>");
  const PageContent page = document.interpret();
  CHECK(page.glyphs.empty());
  REQUIRE(!page.faults.empty());
  CHECK(page.faults.front().find("deeper than 32") != std::string::npos);
  CHECK(page.streams.size() == 33U);
}

TEST_CASE("image draws are numbered in draw order through forms") {
  TextDocument document;
  const std::uint32_t image = document.add_stream(
      "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8",
      std::string(1, '\x80'));
  const std::uint32_t form = document.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << /XObject << /Im " +
          std::to_string(image) + " 0 R >> >>",
      "q 20 0 0 20 0 0 cm /Im Do Q");
  document.page(
      "q 10 0 0 10 50 50 cm BI /W 1 /H 1 /CS /G /BPC 8 ID \x7f EI Q /Fm Do q 30 0 0 30 0 0 cm /Im "
      "Do Q",
      "/XObject << /Im " + std::to_string(image) + " 0 R /Fm " + std::to_string(form) + " 0 R >>");
  const PageContent page = document.interpret();
  REQUIRE(page.images.size() == 3U);
  CHECK(page.images[0].kind == ImageOccurrence::Kind::inline_image);
  CHECK(page.images[0].ordinal == 0U);
  CHECK(page.images[0].box.left == Approx(50.0));
  CHECK(page.images[0].box.right == Approx(60.0));
  REQUIRE(page.images[0].data != nullptr);
  CHECK(page.images[0].data->size() == 1U);
  CHECK(page.images[1].kind == ImageOccurrence::Kind::xobject);
  CHECK(page.images[1].reference.number == image);
  CHECK(page.images[1].form_path == std::vector<std::uint32_t>{form});
  CHECK(page.images[1].box.right == Approx(20.0));
  CHECK(page.images[1].resource_name == "Im");
  CHECK(page.images[2].ordinal == 2U);
  CHECK(page.images[2].form_path.empty());
  CHECK(page.images[2].box.top == Approx(30.0));
}

TEST_CASE("path painting between q and Q is one figure with a paint per operator") {
  const PageContent page = one_page(
      "q 0 0 m 10 10 l S 1 0 0 rg 0 0 100 50 re f Q q 1 g 5 5 1 1 re f Q 0 0 1 1 re W n");
  REQUIRE(page.figures.size() == 2U);
  CHECK(page.figures[0].paints.size() == 2U);
  CHECK(page.figures[0].first_operation == 3U);
  CHECK(page.figures[0].box.right == Approx(100.0));
  CHECK(page.figures[0].paints[0].stroked);
  CHECK_FALSE(page.figures[0].paints[0].filled);
  CHECK(page.figures[0].paints[1].rectangular);
  CHECK_FALSE(page.figures[0].paints[1].white);
  CHECK(page.figures[1].paints[0].white);
}

TEST_CASE("W intersects the clip with the path's bound at the next painting operator") {
  const PageContent page =
      one_page("q 0 0 100 100 re W n BT /F1 10 Tf 10 10 Td (A) Tj ET Q BT /F1 10 Tf (B) Tj ET");
  REQUIRE(page.glyphs.size() == 2U);
  CHECK(page.glyphs[0].clip.right == Approx(100.0));
  CHECK(page.glyphs[1].clip.is_infinite());
}

TEST_CASE("colours are recorded as the content set them") {
  const PageContent page = one_page(
      "BT /F1 10 Tf 1 0 0 rg (A) Tj 0.5 g (A) Tj /CS0 cs 0.2 0.3 0.4 sc (A) Tj 0 0 0 0 k (A) Tj ET",
      "/ColorSpace << /CS0 /DeviceRGB >>");
  REQUIRE(page.glyphs.size() == 4U);
  CHECK(page.glyphs[0].fill.family == "DeviceRGB");
  CHECK(page.glyphs[0].fill.components == std::vector<double>{1.0, 0.0, 0.0});
  CHECK(page.glyphs[1].fill.family == "DeviceGray");
  CHECK(page.glyphs[2].fill.space == "CS0");
  CHECK(page.glyphs[2].fill.family == "DeviceRGB");
  CHECK(page.glyphs[2].fill.components.size() == 3U);
  CHECK(page.glyphs[3].fill.is_white());
}

TEST_CASE("marked content and optional content reach every glyph inside them") {
  TextDocument document;
  const std::uint32_t off = document.add("<< /Type /OCG /Name (Hidden) >>");
  const std::uint32_t on = document.add("<< /Type /OCG /Name (Shown) >>");
  document.catalog_entries("/OCProperties << /OCGs [" + std::to_string(off) + " 0 R " +
                           std::to_string(on) + " 0 R] /D << /OFF [" + std::to_string(off) +
                           " 0 R] >> >>");
  document.page(
      "/P << /MCID 3 >> BDC BT /F1 10 Tf (A) Tj ET EMC /OC /L1 BDC BT /F1 10 Tf (B) Tj ET EMC "
      "/OC /L2 BDC BT /F1 10 Tf (A) Tj ET EMC",
      "/Properties << /L1 " + std::to_string(off) + " 0 R /L2 " + std::to_string(on) + " 0 R >>");
  const PageContent page = document.interpret();
  REQUIRE(page.glyphs.size() == 3U);
  CHECK(page.glyphs[0].tag == "P");
  CHECK(page.glyphs[0].mcid == 3);
  CHECK_FALSE(page.glyphs[0].optional_content_off);
  CHECK(page.glyphs[1].optional_content_off);
  CHECK(page.glyphs[1].optional_content == std::vector<std::uint32_t>{off});
  CHECK_FALSE(page.glyphs[2].optional_content_off);
}

TEST_CASE("an unknown operator, a missing font and an unmatched Q change nothing") {
  const PageContent page = one_page("Q BT /F9 10 Tf (A) Tj /F1 10 Tf foo (A) Tj ET Q Q");
  REQUIRE(page.glyphs.size() == 1U);
  CHECK(page.glyphs[0].text == "A");
}

TEST_CASE("unreadable codes are counted per font name") {
  TextDocument document;
  const std::uint32_t bare = document.add("<< /Type /Font /Subtype /Type1 /BaseFont /Mystery >>");
  document.page("BT /F9 10 Tf <0141> Tj ET", "", "", "/F9 " + std::to_string(bare) + " 0 R");
  const PageContent page = document.interpret();
  REQUIRE(page.glyphs.size() == 2U);
  CHECK(page.glyphs[0].rung == pdfkit::fonts::DecodingRung::unreadable);
  CHECK(page.glyphs[0].text.empty());
  CHECK(page.unreadable.at("Mystery") == 1U);
  CHECK(page.estimated_widths.contains("Mystery"));
}

TEST_CASE("a q stack past 256 saves nothing more and Q still balances") {
  std::string content;
  for (int i = 0; i < 300; ++i) {
    content += "q ";
  }
  content += "BT /F1 10 Tf (A) Tj ET";
  for (int i = 0; i < 300; ++i) {
    content += " Q";
  }
  const PageContent page = one_page(content);
  CHECK(page.glyphs.size() == 1U);
  REQUIRE(!page.faults.empty());
  CHECK(page.faults.front().find("deeper than 256") != std::string::npos);
}
