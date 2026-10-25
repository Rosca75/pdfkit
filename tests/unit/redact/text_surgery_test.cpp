// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The position test of the text surgery (docs/DESIGN.md section 12): a page
// is interpreted, a set of its glyphs is removed by redact::TextSurgery, the
// rewritten stream is drawn as the content of a fresh page with the same
// fonts and interpreted again, and every kept glyph must come back with its
// text and its device-space origin within 0.01 pt of the original, while no
// removed glyph comes back. Each case is one shape a producer writes: a word
// in a TJ array, in a Tj followed by more text, in ' and " (with the Tw and
// Tc " sets), in a two-byte Identity-H string, a removal across a TJ number,
// across two operators and a font change, across a line break with its
// hyphen, under Tz 50, under a rotated text matrix, in render mode 3, a whole
// operator removed, a literal string with escapes and parentheses, and an
// operator whose codes cannot be placed losing all its glyphs.

#include "redact/text_surgery.hpp"

#include "text/content_serialiser.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <cmath>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace {

using pdfkit::redact::TextSurgery;
using pdfkit::test::TextDocument;
using pdfkit::text::GlyphRecord;
using pdfkit::text::PageContent;

struct Rewritten {
  std::string content;
  PageContent before;
  PageContent after;
  std::set<std::size_t> removed;
};

// Removes the glyphs of page 1 of content that chosen selects (by draw index
// and record) and draws the result on a fresh page.
Rewritten rewrite(const std::string& content,
                  const std::function<bool(std::size_t, const GlyphRecord&)>& chosen) {
  Rewritten out;
  TextDocument source;
  source.page(content);
  out.before = source.interpret();
  for (std::size_t i = 0; i < out.before.glyphs.size(); ++i) {
    if (chosen(i, out.before.glyphs[i])) {
      out.removed.insert(i);
    }
  }
  const TextSurgery::Plan plan =
      TextSurgery::plan(out.before.streams[0], 0, out.before.glyphs, out.removed);
  const auto written = pdfkit::text::ContentSerialiser::write(out.before.streams[0], 0,
                                                              out.before.glyphs, plan.edits);
  for (const std::byte value : written.bytes) {
    out.content.push_back(static_cast<char>(value));
  }
  TextDocument target;
  target.page(out.content);
  out.after = target.interpret();
  return out;
}

// Every kept glyph is back, in order, with its text and origin.
void check_kept(const Rewritten& result) {
  INFO("rewritten content: " << result.content);
  std::vector<const GlyphRecord*> kept;
  for (std::size_t i = 0; i < result.before.glyphs.size(); ++i) {
    if (!result.removed.contains(i)) {
      kept.push_back(&result.before.glyphs[i]);
    }
  }
  REQUIRE(result.after.glyphs.size() == kept.size());
  for (std::size_t i = 0; i < kept.size(); ++i) {
    const GlyphRecord& was = *kept[i];
    const GlyphRecord& now = result.after.glyphs[i];
    INFO("kept glyph " << i << " '" << was.text << "'");
    CHECK(now.text == was.text);
    CHECK(std::abs(now.origin.x - was.origin.x) < 0.01);
    CHECK(std::abs(now.origin.y - was.origin.y) < 0.01);
    CHECK(now.render_mode == was.render_mode);
  }
}

std::string drawn(const PageContent& content) { return TextDocument::drawn_text(content); }

std::function<bool(std::size_t, const GlyphRecord&)> draws(std::size_t first, std::size_t last) {
  return [first, last](std::size_t i, const GlyphRecord&) { return i >= first && i < last; };
}

}  // namespace

TEST_CASE("a word leaves a TJ array and the glyphs after it stay put") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 72 700 Td [(Hello) -250 (World) -250 (again)] TJ (tail) Tj ET",
              draws(5, 10));
  CHECK(drawn(result.after) == "Helloagaintail");
  check_kept(result);
}

TEST_CASE("a word leaves a Tj and the text drawn after the operator stays put") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 72 700 Td (Hello World again) Tj (tail) Tj ET", draws(6, 11));
  CHECK(drawn(result.after) == "Hello  againtail");
  check_kept(result);
}

TEST_CASE("a quote keeps its line advance when its string is cut") {
  const Rewritten result = rewrite(
      "BT /F1 10 Tf 14 TL 72 700 Td (first) Tj (Hello World) ' (next) ' ET", draws(11, 16));
  CHECK(drawn(result.after) == "firstHello next");
  check_kept(result);
}

TEST_CASE("a double quote keeps its word and character spacing when its string is cut") {
  const Rewritten result = rewrite(
      "BT /F1 10 Tf 14 TL 72 700 Td (first) Tj 3 1.5 (Hello World x) \" (next) ' ET",
      draws(11, 16));
  CHECK(drawn(result.after) == "firstHello  xnext");
  check_kept(result);
}

TEST_CASE("a quote whose codes are all removed still advances the line") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 14 TL 72 700 Td (first) Tj (Gone) ' (next) ' ET", draws(5, 9));
  CHECK(drawn(result.after) == "firstnext");
  check_kept(result);
}

TEST_CASE("a two-byte code is cut on its boundary") {
  const Rewritten result =
      rewrite("BT /F2 12 Tf 72 700 Td <0041004200410042> Tj <0041> Tj ET", draws(1, 3));
  CHECK(result.after.glyphs.size() == 3);
  check_kept(result);
}

TEST_CASE("a removal crosses a TJ number") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 72 700 Td [(Ja) -20 (ne) -250 (Doe) 30 (s)] TJ ET", draws(0, 7));
  CHECK(drawn(result.after) == "s");
  check_kept(result);
}

TEST_CASE("a removal crosses two operators and a font change") {
  const Rewritten result = rewrite(
      "BT /F1 10 Tf 72 700 Td (Hi Ja) Tj /F2 10 Tf <0041> Tj /F1 12 Tf (ne Doe) Tj ET",
      draws(3, 8));
  CHECK(drawn(result.after) == "Hi  Doe");
  check_kept(result);
}

TEST_CASE("a removal crosses a line break and takes the hyphen") {
  const Rewritten result = rewrite(
      "BT /F1 10 Tf 14 TL 72 700 Td (Zebulon Quarter-) Tj T* (field lives) Tj ET",
      [](std::size_t i, const GlyphRecord&) { return i < 16 || (i >= 16 && i < 21); });
  CHECK(drawn(result.after) == " lives");
  check_kept(result);
}

TEST_CASE("a removal under horizontal scaling keeps the scaled positions") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 50 Tz 72 700 Td [(Hello) -100 (World)] TJ (tail) Tj ET", draws(2, 7));
  check_kept(result);
}

TEST_CASE("a removal under a rotated text matrix keeps the rotated positions") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 0 1 -1 0 300 300 Tm (Hello World) Tj (tail) Tj ET", draws(0, 6));
  check_kept(result);
}

TEST_CASE("invisible text is removed the same way") {
  const Rewritten result =
      rewrite("BT 3 Tr /F1 10 Tf 72 700 Td (Hidden Name here) Tj ET", draws(7, 11));
  CHECK(drawn(result.after) == "Hidden  here");
  check_kept(result);
}

TEST_CASE("a Tj whose codes are all removed becomes a TJ number") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 72 700 Td (Jane Doe) Tj ( tail) Tj ET", draws(0, 8));
  CHECK(drawn(result.after) == " tail");
  CHECK(result.content.find("Jane") == std::string::npos);
  check_kept(result);
}

TEST_CASE("character and word spacing are part of the hole") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 2 Tc 3 Tw 72 700 Td (a bb c) Tj (tail) Tj ET", draws(1, 4));
  CHECK(drawn(result.after) == "a ctail");
  check_kept(result);
}

TEST_CASE("a literal string with escapes and parentheses is rewritten whole") {
  const Rewritten result =
      rewrite("BT /F1 10 Tf 72 700 Td (a\\(b\\)c \\\\ (d) e) Tj ET", draws(2, 3));
  CHECK(drawn(result.after) == "a()c \\ (d) e");
  check_kept(result);
}

TEST_CASE("a hexadecimal string stays hexadecimal") {
  const Rewritten result = rewrite("BT /F1 10 Tf 72 700 Td <4A616E65> Tj ET", draws(1, 2));
  CHECK(drawn(result.after) == "Jne");
  CHECK(result.content.find("<4A>") != std::string::npos);
  check_kept(result);
}

TEST_CASE("the hole number of a glyph is its advance in thousandths of text space") {
  GlyphRecord record;
  record.advance = 7.22 * 0.5;
  record.font_size = 10.0;
  record.horizontal_scaling = 50.0;
  CHECK(std::abs(TextSurgery::hole_number(record) + 722.0) < 1e-9);
  record.font_size = 0.0;
  CHECK(TextSurgery::hole_number(record) == 0.0);
}
