// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// What removing text drawn in render mode 7 writes (docs/DESIGN.md section
// 12, Clip text): a removed glyph leaves the operator that showed it and so
// adds nothing to the clip at ET, the kept glyphs of the text object still
// do, the placeholder over glyphs that are all invisible is drawn in mode 3
// and adds nothing either, and what is painted after ET is written as it
// was. When every clip glyph of a text object goes, the text object shows no
// glyph in a clip mode at all.

#include "redact/support/redaction.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>
#include <vector>

namespace {

// A word shown in mode 7, then a rectangle painted through the clip it sets.
std::string clipped_word() {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "q BT 7 Tr /F1 40 Tf 72 600 Td (SECRET) Tj ET 0 0 1 rg 0 0 612 792 re f Q "
      "BT /F1 10 Tf 72 500 Td (visible) Tj ET");
  return fixture.bytes();
}

struct Drawn {
  std::string clip;
  std::string invisible;
  std::size_t figures = 0;
};

// The glyphs of the output's page added to the clip (mode 7) and drawn in
// mode 3, and the figures it paints.
Drawn drawn(const std::string& output) {
  const std::vector<pdfkit::text::PageText> pages = pdfkit::test::read_pages(output);
  REQUIRE(pages.size() == 1);
  Drawn out;
  for (const pdfkit::text::GlyphRecord& record : pages.front().content.glyphs) {
    if (record.render_mode == 7) {
      out.clip += record.text;
    } else if (record.render_mode == 3) {
      out.invisible += record.text;
    }
  }
  out.figures = pages.front().content.figures.size();
  return out;
}

}  // namespace

TEST_CASE("a removed mode-7 glyph leaves the clip and the kept ones stay in it") {
  const std::string file = clipped_word();
  const Drawn before = drawn(file);
  CHECK(before.clip == "SECRET");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, R"({"matches": [{"search": "CR", "placeholder": "[X]"}]})");
  INFO(result.matches, result.report);
  REQUIRE(result.match_status == PDFKIT_OK);
  REQUIRE(result.write_status == PDFKIT_OK);
  const Drawn after = drawn(result.pdf);
  CHECK(after.clip == "SEET");
  CHECK(after.invisible == "[X]");
  CHECK(after.figures == before.figures);
}

TEST_CASE("removing every clip glyph of a text object leaves it showing none in a clip mode") {
  const std::string file = clipped_word();
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, R"({"matches": [{"search": "SECRET", "placeholder": ""}]})");
  INFO(result.matches, result.report);
  REQUIRE(result.match_status == PDFKIT_OK);
  REQUIRE(result.write_status == PDFKIT_OK);
  const Drawn after = drawn(result.pdf);
  CHECK(after.clip.empty());
  CHECK(after.invisible.empty());
  // The rectangle is still painted after the text object, now through the
  // clip the page had before BT.
  CHECK(after.figures == drawn(file).figures);
  CHECK(pdfkit::test::page_text(result.pdf) == "visible");
}
