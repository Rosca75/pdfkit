// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of redact::PlaceholderFitting (docs/DESIGN.md section 13): the three
// outcomes with their numbers, for a bracketed placeholder and for realistic
// text with lower case, spaces and accents, the hole running to the next
// kept glyph of the line, the bridge to the next kept word where a word
// break stood before it and none inside a word or at a line's end, an
// overflow that would end at the next word drawn over its start, bold
// chosen from the font's weight and from a name holding "Bold", and a
// placeholder outside the alphabet or not UTF-8 refused with the three-part
// message naming the code point, before anything is recorded on the edit
// plan.

#include "redact/placeholder_fitting.hpp"

#include "platform/error.hpp"
#include "redact/support/redaction.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

using pdfkit::redact::PlaceholderFitting;
using pdfkit::text::GlyphRecord;
using Outcome = PlaceholderFitting::Outcome;

struct Fitted {
  pdfkit::text::PageContent content;
  PlaceholderFitting::Result result;
};

// Fits placeholder into the hole of glyphs [first, last) of the page drawn by
// content, the rest of the glyphs kept.
Fitted fit(const std::string& content, std::size_t first, std::size_t last,
           const std::string& placeholder, const std::string& fonts = "") {
  pdfkit::test::TextDocument document;
  document.page(content, "", "", fonts);
  Fitted out;
  out.content = document.interpret();
  std::vector<const GlyphRecord*> removed;
  std::vector<const GlyphRecord*> kept;
  for (std::size_t i = 0; i < out.content.glyphs.size(); ++i) {
    (i >= first && i < last ? removed : kept).push_back(&out.content.glyphs[i]);
  }
  out.result = PlaceholderFitting::fit(removed, kept, placeholder);
  return out;
}

}  // namespace

TEST_CASE("a placeholder that fits its hole is drawn at its natural width") {
  // "Jonathan Smithfield" at 10 pt Helvetica: the hole of "Smithfield" and
  // the space after it is wider than "[X]".
  const Fitted fitted = fit("BT /F1 10 Tf 72 700 Td (Jonathan Smithfield end) Tj ET", 9, 19, "[X]");
  CHECK(fitted.result.outcome == Outcome::natural);
  CHECK(fitted.result.scale == 1.0);
  // The hole runs from the S to the e of "end": "Smithfield " in Helvetica.
  const GlyphRecord& s = fitted.content.glyphs[9];
  const GlyphRecord& e = fitted.content.glyphs[20];
  CHECK(fitted.result.hole == doctest::Approx(e.origin.x - s.origin.x));
  CHECK(fitted.result.anchor == &fitted.content.glyphs[9]);
  CHECK(fitted.result.face == pdfkit::writer::PlaceholderFont::Face::regular);
}

TEST_CASE("a placeholder a little wider than its hole is narrowed") {
  // "Jane Doe" with the space after it (45.6 pt) against "[REDACTED]" at
  // 10 pt (60 pt natural): narrowed to about three quarters.
  const Fitted fitted = fit("BT /F1 10 Tf 72 700 Td (Jane Doe met) Tj ET", 0, 8, "[REDACTED]");
  CHECK(fitted.result.outcome == Outcome::scaled);
  CHECK(fitted.result.scale < 1.0);
  CHECK(fitted.result.scale >= PlaceholderFitting::floor);
  // It stops 0.02 pt short of the m, where the space bridging to it starts.
  CHECK(fitted.result.natural * fitted.result.scale == doctest::Approx(fitted.result.hole - 0.02));
}

TEST_CASE("a placeholder much wider than its hole overflows at the floor") {
  const Fitted fitted = fit("BT /F1 10 Tf 72 700 Td (I met) Tj ET", 0, 1, "[REDACTED]");
  CHECK(fitted.result.outcome == Outcome::overflow);
  CHECK(fitted.result.scale == PlaceholderFitting::floor);
  CHECK(fitted.result.box.right > fitted.content.glyphs[2].origin.x);
}

TEST_CASE("a run at the end of its line has a hole of its own advance") {
  const Fitted fitted = fit("BT /F1 10 Tf 72 700 Td (end Smithfield) Tj ET", 4, 14, "[X]");
  const GlyphRecord& s = fitted.content.glyphs[4];
  const GlyphRecord& d = fitted.content.glyphs[13];
  CHECK(fitted.result.hole == doctest::Approx(d.advance_end.x - s.origin.x));
  CHECK_FALSE(fitted.result.bridge);
}

TEST_CASE("a placeholder before the next word is bridged to it by a space") {
  // The space's advance at 10 pt, Tz 100 and an identity matrix, (w0 * Tfs
  // + Tc), runs from the placeholder's end to the e of "end".
  const double space =
      pdfkit::writer::PlaceholderFont::width(" ", pdfkit::writer::PlaceholderFont::Face::regular) /
      100.0;
  // With its spaces drawn, "Smithfield" is glyphs 9 to 18; placed by TJ
  // numbers alone, 8 to 17.
  for (const auto& [line, first] : std::vector<std::pair<std::string, std::size_t>>{
           {"(Jonathan Smithfield end) Tj", 9},
           {"[(Jonathan) -250 (Smithfield) -250 (end)] TJ", 8}}) {
    INFO(line);
    const Fitted fitted = fit("BT /F1 10 Tf 72 700 Td " + line + " ET", first, first + 10, "[X]");
    CHECK(fitted.result.outcome == Outcome::natural);
    REQUIRE(fitted.result.bridge);
    CHECK(space + fitted.result.bridge_spacing ==
          doctest::Approx(fitted.result.hole - fitted.result.natural));
  }
  // Narrowed to fill the hole, the placeholder leaves the space its least
  // advance, 0.02 pt, under the placeholder's horizontal scaling.
  const Fitted filled = fit("BT /F1 10 Tf 72 700 Td (Jane Doe met) Tj ET", 0, 8, "[REDACTED]");
  CHECK(filled.result.outcome == Outcome::scaled);
  REQUIRE(filled.result.bridge);
  CHECK((space + filled.result.bridge_spacing) * filled.result.scale == doctest::Approx(0.02));
}

TEST_CASE("a match inside a word is not bridged to the rest of the word") {
  const Fitted fitted = fit("BT /F1 10 Tf 72 700 Td (Smithson met) Tj ET", 0, 5, "[X]");
  CHECK_FALSE(fitted.result.bridge);
}

TEST_CASE("an overflow that would end at the next word is drawn over its start") {
  // "Doe " is 21.12 pt at 10 pt and "[NAMEI]" 37.23 pt: at the floor it
  // would end 1.2 pt past the m of "met", so it runs 1.5 pt (0.15 of the
  // size) past it.
  const Fitted fitted = fit("BT /F1 10 Tf 72 700 Td (Doe met) Tj ET", 0, 3, "[NAMEI]");
  CHECK(fitted.result.outcome == Outcome::overflow);
  CHECK(fitted.result.scale > PlaceholderFitting::floor);
  CHECK(fitted.result.natural * fitted.result.scale == doctest::Approx(fitted.result.hole + 1.5));
  CHECK_FALSE(fitted.result.bridge);
}

TEST_CASE("the bold face follows the font's weight and its name") {
  const std::string bold_by_name =
      "/FB << /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>";
  const Fitted named = fit("BT /FB 10 Tf 72 700 Td (Jane Doe) Tj ET", 0, 4, "[X]", bold_by_name);
  CHECK(named.result.face == pdfkit::writer::PlaceholderFont::Face::bold);
  const std::string bold_by_weight =
      "/FW << /Type /Font /Subtype /TrueType /BaseFont /Corporate /FirstChar 32 /LastChar 126 "
      "/Widths [] /FontDescriptor << /Type /FontDescriptor /FontName /Corporate /Flags 32 "
      "/FontWeight 700 /FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 800 /Descent -200 "
      "/CapHeight 700 /StemV 80 >> /Encoding /WinAnsiEncoding >>";
  const Fitted weighted =
      fit("BT /FW 10 Tf 72 700 Td (Jane Doe) Tj ET", 0, 4, "[X]", bold_by_weight);
  CHECK(weighted.result.face == pdfkit::writer::PlaceholderFont::Face::bold);
}

TEST_CASE("a placeholder outside the alphabet is refused with what, why and how") {
  try {
    PlaceholderFitting::require_alphabet("[a\u0180b]", "pdfkit_redact_text match 0");
    FAIL("expected a refusal");
  } catch (const pdfkit::platform::Error& error) {
    CHECK(error.code() == pdfkit::platform::ErrorCode::invalid_argument);
    CHECK(std::string(error.failed()).find("U+0180") != std::string::npos);
    CHECK(std::string(error.expected()).find("Latin Extended-A") != std::string::npos);
    CHECK(std::string(error.fix()).find("[REDACTED]") != std::string::npos);
  }
  try {
    PlaceholderFitting::require_alphabet(std::string("J\xC3r", 3), "pdfkit_redact_text match 2");
    FAIL("expected a refusal");
  } catch (const pdfkit::platform::Error& error) {
    CHECK(error.code() == pdfkit::platform::ErrorCode::invalid_argument);
    CHECK(std::string(error.failed()).find("ill-formed UTF-8") != std::string::npos);
    CHECK(std::string(error.failed()).find("match 2") != std::string::npos);
  }
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Jane Doe) Tj ET");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Jane", "a\u2014b"));
  CHECK(result.match_status == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(result.matches.find("placeholder alphabet") != std::string::npos);
  CHECK(result.matches.find("U+2014") != std::string::npos);
}

TEST_CASE("a lower-case accented placeholder with spaces is fitted by its width") {
  const std::string placeholder = "Jérôme Łukasz";
  // Its natural width in the regular face at 10 pt.
  const double natural = pdfkit::writer::PlaceholderFont::width(
                             placeholder, pdfkit::writer::PlaceholderFont::Face::regular) /
                         100.0;
  CHECK(natural > 50.0);
  CHECK(natural < 80.0);
  // A hole wider than the placeholder: drawn as it is.
  const Fitted wide =
      fit("BT /F1 10 Tf 72 700 Td (Bartholomew Featherstonehaugh end) Tj ET", 0, 29, placeholder);
  CHECK(wide.result.outcome == Outcome::natural);
  CHECK(wide.result.natural == doctest::Approx(natural));
  // A hole a little narrower: narrowed to fit.
  const Fitted close = fit("BT /F1 10 Tf 72 700 Td (Jane Smith met) Tj ET", 0, 10, placeholder);
  CHECK(close.result.outcome == Outcome::scaled);
  CHECK(close.result.natural * close.result.scale == doctest::Approx(close.result.hole - 0.02));
  // A hole far narrower: overflowing at the floor.
  const Fitted narrow = fit("BT /F1 10 Tf 72 700 Td (I met) Tj ET", 0, 1, placeholder);
  CHECK(narrow.result.outcome == Outcome::overflow);
  CHECK(narrow.result.scale == PlaceholderFitting::floor);
}

TEST_CASE("the fitting is reported per occurrence") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Jane Doe met) Tj ET");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Jane Doe"));
  REQUIRE(result.match_status == PDFKIT_OK);
  CHECK(result.matches.find("\"fitting\":\"scaled\"") != std::string::npos);
  CHECK(result.matches.find("\"count\":1") != std::string::npos);
}
