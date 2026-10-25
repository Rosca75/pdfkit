// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the widths of docs/DESIGN.md section 5.6, each matching the number
// its source states to the unit: /Widths with /FirstChar and /MissingWidth,
// /W in both array forms with /DW and the vertical /W2 and /DW2, a TrueType
// program's hmtx scaled by its units per em (fewer metrics than glyphs, the
// last advance repeating), a CFF width (nominal and default), a Type 1 hsbw
// width, a Core 14 width through an alias and through a subset prefix, a
// Type 3 width through its matrix, and the vertical metrics' sources.

#include "fonts/pdf_font.hpp"
#include "fonts/standard_fonts.hpp"
#include "support/font_builder.hpp"
#include "support/font_document.hpp"
#include "support/require_value.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::fonts::PdfFont;
using pdfkit::fonts::StandardFonts;
using pdfkit::test::FontBuilder;
using pdfkit::test::FontDocument;
using pdfkit::test::require_value;

TEST_CASE("/Widths from /FirstChar, and /MissingWidth outside it") {
  FontDocument doc;
  const auto descriptor =
      doc.add("<< /Type /FontDescriptor /FontName /X /Flags 32 /MissingWidth 250 >>");
  const auto font = doc.add(
      "<< /Type /Font /Subtype /TrueType /BaseFont /X /FirstChar 65 /Widths "
      "[600 (bad) 700] /FontDescriptor " +
      std::to_string(descriptor) + " 0 R >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->width(65) == doctest::Approx(600));
  CHECK(loaded->width(66) == doctest::Approx(250));
  CHECK(loaded->width(67) == doctest::Approx(700));
  CHECK(loaded->width(64) == doctest::Approx(250));
  CHECK(loaded->width(68) == doctest::Approx(250));
  CHECK_FALSE(loaded->widths_estimated());
}

TEST_CASE("/W in both forms with /DW, and /W2 with /DW2") {
  FontDocument doc;
  const auto cid_font = doc.add(
      "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /C /DW 900 "
      "/W [1 [100 200 300] 10 20 555] /DW2 [800 -900] "
      "/W2 [5 [-1000 250 880] 30 31 -500 100 700] >>");
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type0 /BaseFont /C /Encoding /Identity-V "
      "/DescendantFonts [" +
      std::to_string(cid_font) + " 0 R] >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->is_vertical());
  CHECK(loaded->width(1) == doctest::Approx(100));
  CHECK(loaded->width(3) == doctest::Approx(300));
  CHECK(loaded->width(4) == doctest::Approx(900));
  CHECK(loaded->width(10) == doctest::Approx(555));
  CHECK(loaded->width(20) == doctest::Approx(555));
  CHECK(loaded->width(21) == doctest::Approx(900));
  const PdfFont::VerticalAdvance listed = loaded->vertical_advance(5);
  CHECK(listed.w1 == doctest::Approx(-1000));
  CHECK(listed.vx == doctest::Approx(250));
  CHECK(listed.vy == doctest::Approx(880));
  const PdfFont::VerticalAdvance ranged = loaded->vertical_advance(31);
  CHECK(ranged.w1 == doctest::Approx(-500));
  const PdfFont::VerticalAdvance fallback = loaded->vertical_advance(2);
  CHECK(fallback.w1 == doctest::Approx(-900));
  CHECK(fallback.vy == doctest::Approx(800));
  CHECK(fallback.vx == doctest::Approx(100));
}

TEST_CASE("a CID font without /W or /DW is 1000 wide") {
  FontDocument doc;
  const auto cid_font = doc.add("<< /Type /Font /Subtype /CIDFontType0 /BaseFont /C >>");
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type0 /BaseFont /C /Encoding /Identity-H "
      "/DescendantFonts [" +
      std::to_string(cid_font) + " 0 R] >>");
  CHECK(doc.load(font)->width(42) == doctest::Approx(1000));
}

TEST_CASE("a TrueType program's hmtx scaled by its units per em") {
  FontDocument doc;
  FontBuilder::TrueType spec;
  spec.units_per_em = 2048;
  spec.advances = {1024, 2048};
  spec.glyph_count = 4;
  spec.cmaps.push_back({3, 1, 4, {{0x41, 1}, {0x42, 3}}});
  const auto program = doc.add_stream("", FontBuilder::true_type(spec));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /T /Flags 32 /FontFile2 " +
                                  std::to_string(program) + " 0 R >>");
  const auto font = doc.add(
      "<< /Type /Font /Subtype /TrueType /BaseFont /T /Encoding "
      "/WinAnsiEncoding /FontDescriptor " +
      std::to_string(descriptor) + " 0 R >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->width(0x41) == doctest::Approx(1000));
  // Glyph 3 is past hhea's two metrics: the last advance repeats.
  CHECK(loaded->width(0x42) == doctest::Approx(1000));
  CHECK(loaded->vertical_metrics().ascent == doctest::Approx(800.0 * 1000 / 2048));
}

TEST_CASE("a CFF width: nominal plus the charstring's operand, or the default") {
  FontDocument doc;
  FontBuilder::Cff spec;
  spec.glyph_names = {".notdef", "A", "B"};
  spec.widths = {std::nullopt, 22, std::nullopt};
  spec.nominal_width = 600;
  spec.default_width = 444;
  const auto program = doc.add_stream("/Subtype /Type1C", FontBuilder::cff(spec));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /F /Flags 32 /FontFile3 " +
                                  std::to_string(program) + " 0 R >>");
  const auto font = doc.add("<< /Type /Font /Subtype /Type1 /BaseFont /F /FontDescriptor " +
                            std::to_string(descriptor) + " 0 R >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->width(0x41) == doctest::Approx(622));
  CHECK(loaded->width(0x42) == doctest::Approx(444));
}

TEST_CASE("a Type 1 width from its hsbw command") {
  FontDocument doc;
  FontBuilder::Type1 spec;
  spec.glyphs = {{".notdef", 0}, {"A", 722}, {"B", 667}};
  const auto program = doc.add_stream("", FontBuilder::type1(spec));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /T1 /Flags 32 /FontFile " +
                                  std::to_string(program) + " 0 R >>");
  const auto font = doc.add("<< /Type /Font /Subtype /Type1 /BaseFont /T1 /FontDescriptor " +
                            std::to_string(descriptor) + " 0 R >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->width(0x41) == doctest::Approx(722));
  CHECK(loaded->width(0x42) == doctest::Approx(667));
  CHECK(loaded->width(0x43) == doctest::Approx(0));
}

TEST_CASE("Core 14 widths through an alias and a subset prefix") {
  FontDocument doc;
  const auto alias = doc.add(
      "<< /Type /Font /Subtype /TrueType /BaseFont /Arial,Bold /Encoding "
      "/WinAnsiEncoding >>");
  const auto subset = doc.add("<< /Type /Font /Subtype /Type1 /BaseFont /ABCDEF+Times-Roman >>");
  const auto unknown = doc.add("<< /Type /Font /Subtype /TrueType /BaseFont /Calibri >>");
  const auto bold = doc.load(alias);
  // Helvetica-Bold's AFM: A 722, a 556, space 278.
  CHECK(bold->width('A') == doctest::Approx(722));
  CHECK(bold->width('a') == doctest::Approx(556));
  CHECK(bold->width(' ') == doctest::Approx(278));
  CHECK(bold->is_bold());
  CHECK(bold->vertical_metrics().ascent == doctest::Approx(718));
  const auto times = doc.load(subset);
  // Times-Roman's AFM: A 722, a 444.
  CHECK(times->width('A') == doctest::Approx(722));
  CHECK(times->width('a') == doctest::Approx(444));
  CHECK_FALSE(times->widths_estimated());
  const auto estimated = doc.load(unknown);
  CHECK(estimated->widths_estimated());
  CHECK(estimated->width('A') == doctest::Approx(0));
  CHECK(estimated->vertical_metrics().fallback);
  CHECK(estimated->vertical_metrics().ascent == doctest::Approx(800));
}

TEST_CASE("the alias table names the Core 14 face of each spelling") {
  using Face = StandardFonts::Face;
  CHECK(StandardFonts::find("ArialMT") == Face::helvetica);
  CHECK(StandardFonts::find("Arial-BoldMT") == Face::helvetica_bold);
  CHECK(StandardFonts::find("Arial,BoldItalic") == Face::helvetica_bold_oblique);
  CHECK(StandardFonts::find("TimesNewRomanPSMT") == Face::times_roman);
  CHECK(StandardFonts::find("TimesNewRoman,Italic") == Face::times_italic);
  CHECK(StandardFonts::find("TimesNewRomanPS-BoldItalicMT") == Face::times_bold_italic);
  CHECK(StandardFonts::find("CourierNew") == Face::courier);
  CHECK(StandardFonts::find("CourierNewPS-BoldMT") == Face::courier_bold);
  CHECK(StandardFonts::find("ABCDEF+Helvetica-Oblique") == Face::helvetica_oblique);
  CHECK(StandardFonts::find("Symbol") == Face::symbol);
  CHECK(StandardFonts::find("ZapfDingbats") == Face::zapf_dingbats);
  CHECK_FALSE(StandardFonts::find("Calibri").has_value());
  CHECK_FALSE(StandardFonts::find("ABCDE+Helvetica").has_value());
  CHECK(StandardFonts::strip_subset("ABCDEF+Name") == "Name");
  CHECK(StandardFonts::strip_subset("abcdef+Name") == "abcdef+Name");
}

TEST_CASE("a Type 3 width in glyph space through /FontMatrix") {
  FontDocument doc;
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type3 /FontBBox [0 -10 100 90] /FontMatrix "
      "[0.01 0 0 0.01 0 0] /FirstChar 1 /LastChar 2 /Widths [50 80] "
      "/CharProcs << >> /Encoding << /Differences [1 /a /b] >> >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->width(1) == doctest::Approx(500));
  CHECK(loaded->width(2) == doctest::Approx(800));
  CHECK(loaded->vertical_metrics().ascent == doctest::Approx(900));
  CHECK(loaded->vertical_metrics().descent == doctest::Approx(-100));
}

TEST_CASE("ascent and descent from the descriptor before the fallback") {
  FontDocument doc;
  const auto descriptor = doc.add(
      "<< /Type /FontDescriptor /FontName /D /Flags 32 /Ascent 905 "
      "/Descent -212 /FontWeight 700 >>");
  const auto font = doc.add("<< /Type /Font /Subtype /TrueType /BaseFont /D /FontDescriptor " +
                            std::to_string(descriptor) + " 0 R >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->vertical_metrics().ascent == doctest::Approx(905));
  CHECK(loaded->vertical_metrics().descent == doctest::Approx(-212));
  CHECK_FALSE(loaded->vertical_metrics().fallback);
  CHECK(loaded->is_bold());
  CHECK(loaded->name() == "D");
}

}  // namespace
