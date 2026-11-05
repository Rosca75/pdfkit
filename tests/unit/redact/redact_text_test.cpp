// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Text redaction through the C ABI, end to end: the matched text leaves the
// output's text and bytes, every other glyph is read back at its place, the
// placeholder is read where the match was, the write report names the
// rectangles and the self-check's pass; the cases a producer writes (two
// /Contents pieces, a two-byte font, hidden text, a fake-bold duplicate, a
// page selection) and the refusals of a glyph range (another document's
// fingerprint, a range past its page) and of malformed matches. A
// placeholder of lower case, spaces, punctuation and accented letters, and a
// mask of asterisks, read back where the match was. A box match removes the
// glyphs and paints one opaque filled rectangle per reading line inside the
// reported rectangles, before the text object so its clip does not cut the
// box, in the colour asked, and none over a run whose glyphs are all hidden;
// a malformed style or colour is refused with the three parts.

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "redact/support/redaction.hpp"
#include "support/pdf_builder.hpp"
#include "text/support/text_document.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

using pdfkit::test::page_text;
using pdfkit::test::redact;
using pdfkit::test::search;

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;

// A rectangle of the answer, [left, bottom, right, top].
pdfkit::text::Rect rect_of(const JsonValue& value) {
  const JsonValue::Array& items = value.as_array();
  return pdfkit::text::Rect::of(items[0].as_number(), items[1].as_number(), items[2].as_number(),
                                items[3].as_number());
}

// The first occurrence of the first match of an answer.
JsonValue first_occurrence(const std::string& answer) {
  const JsonValue parsed = JsonReader::parse(answer);
  return parsed.find("matches")->as_array().front().find("occurrences")->as_array().front();
}

// The filled paints of page 1 of output.
std::vector<pdfkit::text::FigureRecord::Paint> fills(const std::string& output) {
  std::vector<pdfkit::text::FigureRecord::Paint> out;
  for (const pdfkit::text::FigureRecord& figure :
       pdfkit::test::read_pages(output).front().content.figures) {
    for (const pdfkit::text::FigureRecord::Paint& paint : figure.paints) {
      if (paint.filled) {
        out.push_back(paint);
      }
    }
  }
  return out;
}

// Whether inner lies inside outer, to a hundredth of a point.
bool inside(const pdfkit::text::Rect& inner, const pdfkit::text::Rect& outer) {
  return inner.left >= outer.left - 0.01 && inner.right <= outer.right + 0.01 &&
         inner.bottom >= outer.bottom - 0.01 && inner.top <= outer.top + 0.01;
}

// A box match of the search text with extra keys.
std::string box(const std::string& text, const std::string& extra = "") {
  return R"({"matches": [{"search": ")" + text + R"(", "style": "box")" + extra + "}]}";
}

// The origins of the glyphs of page 1 whose text is not in dropped, in
// draw order.
std::vector<std::pair<std::string, pdfkit::text::Matrix::Point>> origins(
    const pdfkit::text::PageText& page, const std::string& font_prefix_dropped = "") {
  std::vector<std::pair<std::string, pdfkit::text::Matrix::Point>> out;
  for (const pdfkit::text::GlyphRecord& record : page.content.glyphs) {
    if (!font_prefix_dropped.empty() && record.font != nullptr &&
        record.font->name().find(font_prefix_dropped) != std::string::npos) {
      continue;
    }
    out.emplace_back(record.text, record.origin);
  }
  return out;
}

}  // namespace

TEST_CASE("a redacted name leaves the text and the bytes and the rest stays put") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "BT /F1 11 Tf 72 700 Td [(Report on Zebulon) -120 ( Quarterfield, filed today.)] TJ "
      "0 -14 Td (Second line stays.) Tj ET");
  const std::string file = fixture.bytes();
  const pdfkit::test::Redaction result = redact(file, search("Zebulon Quarterfield"));
  REQUIRE(result.match_status == PDFKIT_OK);
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.report);
  const std::string text = page_text(result.pdf);
  CHECK(text.find("[REDACTED]") != std::string::npos);
  CHECK(text.find("filed today.") != std::string::npos);
  CHECK(text.find("Second line stays.") != std::string::npos);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Quarterfield"));
  CHECK(result.report.find("\"self_check\":{\"status\":\"passed\"") != std::string::npos);
  CHECK(result.report.find("\"rectangles\":[{\"page\":1,\"source_page\":1,\"placeholders\":1") !=
        std::string::npos);
  // Every kept glyph at its place.
  const auto before = origins(pdfkit::test::read_pages(file).front());
  const auto after = origins(pdfkit::test::read_pages(result.pdf).front(), "LiberationSans");
  std::vector<std::pair<std::string, pdfkit::text::Matrix::Point>> kept;
  for (std::size_t i = 0; i < before.size(); ++i) {
    // Glyphs 10 to 16 are "Zebulon", 17 the space, 18 to 29 "Quarterfield".
    if (i < 10 || i > 29) {
      kept.push_back(before[i]);
    }
  }
  REQUIRE(after.size() == kept.size());
  for (std::size_t i = 0; i < kept.size(); ++i) {
    CHECK(after[i].first == kept[i].first);
    CHECK(std::abs(after[i].second.x - kept[i].second.x) < 0.01);
    CHECK(std::abs(after[i].second.y - kept[i].second.y) < 0.01);
  }
}

TEST_CASE("a match split across two /Contents pieces is removed from both") {
  pdfkit::test::PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t font = builder.add(
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
  const std::uint32_t first = builder.add_stream("", "BT /F1 10 Tf 72 700 Td (Zebu) Tj");
  const std::uint32_t second = builder.add_stream("", "(lon lives) Tj ET");
  const std::uint32_t page = builder.add_page("", "");
  builder.set(page, "<< /Type /Page /Parent " + std::to_string(builder.page_tree()) +
                        " 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 " +
                        std::to_string(font) + " 0 R >> >> /Contents [" + std::to_string(first) +
                        " 0 R " + std::to_string(second) + " 0 R] >>");
  const pdfkit::test::Redaction result = redact(builder.build(), search("zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(page_text(result.pdf) == "[REDACTED] lives");
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebu"));
}

TEST_CASE("two-byte codes, hidden text and a fake bold duplicate are all removed") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "BT /F2 12 Tf 72 700 Td <00410042004100420041> Tj ET "
      "BT 3 Tr /F1 10 Tf 72 600 Td (secret Zebulon) Tj ET "
      "BT /F1 10 Tf 72 500 Td (Zebulon) Tj ET BT /F1 10 Tf 72.3 500 Td (Zebulon) Tj ET");
  const pdfkit::test::Redaction two_byte = redact(fixture.bytes(), search("BAB"));
  REQUIRE(two_byte.write_status == PDFKIT_OK);
  CHECK(page_text(two_byte.pdf).find("BAB") == std::string::npos);
  CHECK(page_text(two_byte.pdf).find("[REDACTED]") != std::string::npos);
  const pdfkit::test::Redaction hidden = redact(fixture.bytes(), search("zebulon"));
  REQUIRE(hidden.write_status == PDFKIT_OK);
  INFO(hidden.report);
  const std::string text = page_text(hidden.pdf);
  CHECK(text.find("Zebulon") == std::string::npos);
  CHECK(text.find("secret") != std::string::npos);
  CHECK_FALSE(pdfkit::test::contains_marker(hidden.pdf, "Zebulon"));
}

TEST_CASE("an invisible run draws an invisible placeholder") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT 3 Tr /F1 10 Tf 72 600 Td (hidden Zebulon layer) Tj ET");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), search("zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  const auto pages = pdfkit::test::read_pages(result.pdf);
  bool placeholder = false;
  for (const pdfkit::text::GlyphRecord& record : pages.front().content.glyphs) {
    if (record.text == "[") {
      placeholder = true;
      CHECK(record.render_mode == 3);
    }
  }
  CHECK(placeholder);
}

TEST_CASE("a redaction and a page selection work together") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (one Zebulon) Tj ET");
  fixture.page("BT /F1 10 Tf 72 700 Td (two Zebulon) Tj ET");
  fixture.page("BT /F1 10 Tf 72 700 Td (three Zebulon) Tj ET");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), search("Zebulon"), {3, 1});
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(page_text(result.pdf, 1) == "three [REDACTED]");
  CHECK(page_text(result.pdf, 2) == "one [REDACTED]");
  CHECK(result.report.find("{\"page\":1,\"source_page\":3") != std::string::npos);
  CHECK(result.report.find("{\"page\":2,\"source_page\":1") != std::string::npos);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
}

TEST_CASE("the answer lists each match's occurrences with their rectangles") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Zebulon and zebulon) Tj ET");
  const pdfkit::test::Redaction result =
      redact(fixture.bytes(),
             R"({"matches": [{"search": "ZEBULON", "folding": {"case": true}, "placeholder": "[A]"},
                      {"search": "absent"}]})");
  REQUIRE(result.match_status == PDFKIT_OK);
  INFO(result.matches);
  CHECK(result.matches.find("\"count\":2") != std::string::npos);
  CHECK(result.matches.find("\"count\":0") != std::string::npos);
  CHECK(result.matches.find("\"glyphs\":[0,7]") != std::string::npos);
  CHECK(result.matches.find("\"text\":\"Zebulon\"") != std::string::npos);
  CHECK(result.matches.find("\"rectangles\":[[") != std::string::npos);
  CHECK(result.matches.find("\"placeholder\":\"[REDACTED]\"") != std::string::npos);
  const std::string text = page_text(result.pdf);
  CHECK(text.find("[A]") != text.rfind("[A]"));
  CHECK(text.find("ebulon") == std::string::npos);
}

TEST_CASE("a glyph range from another document or past its page is refused") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Zebulon) Tj ET");
  const std::string file = fixture.bytes();
  const std::string stale = R"({"matches": [{"glyphs": {"page": 1, "first": 0, "last": 3},
      "fingerprint": "0000000000000000000000000000000000000000000000000000000000000000"}]})";
  const pdfkit::test::Redaction other = redact(file, stale);
  CHECK(other.match_status == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(other.matches.find("another document") != std::string::npos);
  const pdfkit::test::Redaction probe = redact(file, search("x"));
  const std::size_t at = probe.matches.find(R"("fingerprint":")");
  REQUIRE(at != std::string::npos);
  const std::string fingerprint = probe.matches.substr(at + 15, 64);
  const pdfkit::test::Redaction past = redact(
      file, R"({"matches": [{"glyphs": {"page": 1, "first": 5, "last": 50}, "fingerprint": ")" +
                fingerprint + "\"}]}");
  CHECK(past.match_status == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(past.matches.find("glyph range") != std::string::npos);
  const pdfkit::test::Redaction good = redact(
      file, R"({"matches": [{"glyphs": {"page": 1, "first": 0, "last": 3}, "fingerprint": ")" +
                fingerprint + "\"}]}");
  REQUIRE(good.write_status == PDFKIT_OK);
  CHECK(page_text(good.pdf).ends_with("ulon"));
}

TEST_CASE("malformed matches are refused") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Zebulon) Tj ET");
  const std::string file = fixture.bytes();
  for (const std::string bad :
       {R"({})", R"({"matches": [1]})", R"({"matches": [{"search": "a", "glyphs": {}}]})",
        R"({"matches": [{"search": 5}]})",
        R"({"matches": [{"search": "a", "folding": {"x": true}}]})",
        R"({"matches": [{"search": "a"}], "extra": 1})"}) {
    INFO(bad);
    CHECK(redact(file, bad).match_status == PDFKIT_E_INVALID_ARGUMENT);
  }
}

TEST_CASE("a string no glyph shows goes when it spells the redacted string") {
  pdfkit::test::TextDocument fixture;
  // A Tj before any font, and an operator Annex A does not know, as a
  // damaged stream lexes them.
  fixture.page(
      "BT 72 700 Td (Zebulon unshown) Tj ET q (Zebulon unknown) frob Q "
      "BT /F1 10 Tf 72 600 Td (kept text) Tj ET");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), search("zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.report);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
  CHECK(page_text(result.pdf) == "kept text");
}

TEST_CASE("the self-check reads the changed pages of a search again and every surface") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Zebulon here) Tj ET");
  fixture.page("BT /F1 10 Tf 72 700 Td (nothing to see) Tj ET /Figure << /Alt (plain) >> BDC EMC");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), search("zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(result.report.find("\"self_check\":{\"status\":\"passed\"") != std::string::npos);
  CHECK(page_text(result.pdf, 2) == "nothing to see");
}

TEST_CASE("a comment of a content stream that spells the string is emptied") {
  pdfkit::test::TextDocument fixture;
  fixture.page("% drawn for Zebulon\nBT /F1 10 Tf 72 700 Td (a note) Tj ET % (Zebulon) too\n");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), search("zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
  CHECK(page_text(result.pdf) == "a note");
}

TEST_CASE("an unknown keyword and an unterminated string that spell the string go") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (kept) Tj ET\n1 2 cjZebulon81\nq Q\n(Zebulon\n");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), search("zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.report);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
  CHECK(page_text(result.pdf) == "kept");
}

TEST_CASE("realistic text and a mask are drawn as placeholders and read back") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Report by Zebulon Quarterfield today) Tj ET");
  for (const std::string placeholder : {"Jérôme Łukasz-O'Brien & Co.", "*****"}) {
    INFO(placeholder);
    const pdfkit::test::Redaction result =
        redact(fixture.bytes(), search("Zebulon Quarterfield", placeholder));
    REQUIRE(result.match_status == PDFKIT_OK);
    REQUIRE(result.write_status == PDFKIT_OK);
    const std::string text = page_text(result.pdf);
    CHECK(text.find(placeholder) != std::string::npos);
    CHECK(text.find("Zebulon") == std::string::npos);
    CHECK(result.report.find("\"self_check\":{\"status\":\"passed\"") != std::string::npos);
    CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
  }
}

TEST_CASE("a box match removes the glyphs and paints a box inside the rectangles") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Report on Zebulon today) Tj ET");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), box("Zebulon"));
  REQUIRE(result.match_status == PDFKIT_OK);
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.matches);
  const JsonValue match = JsonReader::parse(result.matches).find("matches")->as_array().front();
  CHECK(match.find("style")->as_string() == "box");
  CHECK(match.find("placeholder")->as_string().empty());
  const JsonValue occurrence = first_occurrence(result.matches);
  CHECK_FALSE(occurrence.find("placeholder_drawn")->as_bool());
  CHECK(occurrence.find("fitting")->kind() == JsonValue::Kind::null);
  REQUIRE(occurrence.find("boxes")->as_array().size() == 1);
  const pdfkit::text::Rect drawn = rect_of(occurrence.find("boxes")->as_array().front());
  const pdfkit::text::Rect changed = rect_of(occurrence.find("rectangles")->as_array().front());
  CHECK(inside(drawn, changed));
  CHECK(drawn.width() > 30.0);
  // The glyphs are gone and no text took their place.
  const std::string text = page_text(result.pdf);
  CHECK(text.find("Zebulon") == std::string::npos);
  CHECK(text.find("Report on") != std::string::npos);
  CHECK(text.find("today") != std::string::npos);
  CHECK(text.find("[REDACTED]") == std::string::npos);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
  CHECK(result.pdf.find("/FontFile2") == std::string::npos);
  // One filled rectangle, the one the answer names, in black by default.
  const auto painted = fills(result.pdf);
  REQUIRE(painted.size() == 1);
  CHECK(painted.front().rectangular);
  CHECK(painted.front().opaque);
  CHECK(inside(painted.front().box, changed));
  CHECK(std::abs(painted.front().box.left - drawn.left) < 0.01);
  CHECK(std::abs(painted.front().box.right - drawn.right) < 0.01);
  const std::string bytes = pdfkit::test::everything(result.pdf);
  CHECK(bytes.find("0 0 0 rg") != std::string::npos);
  CHECK(bytes.find(" re\nf\n") != std::string::npos);
  CHECK(result.report.find("\"self_check\":{\"status\":\"passed\"") != std::string::npos);
}

TEST_CASE("a box match over two lines paints two boxes") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (the Zebulon) Tj 0 -14 Td (Quarterfield here) Tj ET");
  const pdfkit::test::Redaction result =
      redact(fixture.bytes(), box("Zebulon Quarterfield", R"(, "folding": {"whitespace": true})"));
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.matches);
  const JsonValue occurrence = first_occurrence(result.matches);
  REQUIRE(occurrence.find("boxes")->as_array().size() == 2);
  CHECK(fills(result.pdf).size() == 2);
  const std::string text = page_text(result.pdf);
  CHECK(text.find("Zebulon") == std::string::npos);
  CHECK(text.find("Quarterfield") == std::string::npos);
}

TEST_CASE("a run whose glyphs are all hidden draws no box") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT 3 Tr /F1 10 Tf 72 600 Td (hidden Zebulon layer) Tj ET");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), box("Zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.matches);
  CHECK(first_occurrence(result.matches).find("boxes")->as_array().empty());
  CHECK(fills(result.pdf).empty());
  CHECK(page_text(result.pdf).find("Zebulon") == std::string::npos);
}

TEST_CASE("a box takes the colour asked") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Report on Zebulon today) Tj ET");
  const pdfkit::test::Redaction result =
      redact(fixture.bytes(), box("Zebulon", R"(, "colour": [0.2, 0.4, 0.6])"));
  REQUIRE(result.write_status == PDFKIT_OK);
  const std::string bytes = pdfkit::test::everything(result.pdf);
  CHECK(bytes.find("0.2 0.4 0.6 rg") != std::string::npos);
  CHECK(bytes.find("0 0 0 rg") == std::string::npos);
  CHECK(fills(result.pdf).size() == 1);
}

TEST_CASE("a box under a translucent graphics state is painted opaque") {
  pdfkit::test::TextDocument fixture;
  fixture.page("/G1 gs BT /F1 10 Tf 72 700 Td (Report on Zebulon today) Tj ET",
               "/ExtGState << /G1 << /Type /ExtGState /ca 0.3 /CA 0.3 /BM /Multiply >> >>");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), box("Zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  const std::string bytes = pdfkit::test::everything(result.pdf);
  CHECK(bytes.find("/PdfkitBoxGS gs") != std::string::npos);
  const auto painted = fills(result.pdf);
  REQUIRE(painted.size() == 1);
  CHECK(painted.front().opaque);
  // The state is named in the page's resources and resets what a box
  // inherits.
  const auto document = pdfkit::test::reopen(result.pdf);
  const pdfkit::syntax::Object resources = document->resolve(document->pages().front().resources);
  const pdfkit::syntax::Object states = document->resolve(resources.dictionary()->get("ExtGState"));
  REQUIRE(states.dictionary() != nullptr);
  const pdfkit::syntax::Object state = document->resolve(states.dictionary()->get("PdfkitBoxGS"));
  REQUIRE(state.dictionary() != nullptr);
  CHECK(state.dictionary()->get("ca").number() == 1.0);
  CHECK(state.dictionary()->get("CA").number() == 1.0);
  CHECK(state.dictionary()->get("BM").is_name("Normal"));
  CHECK(state.dictionary()->get("SMask").is_name("None"));
}

TEST_CASE("a box in a clipping text object is not cut away by the text clip") {
  pdfkit::test::TextDocument fixture;
  // Render mode 4 fills the glyphs and adds them to the clip at ET: a box
  // painted after that ET would be clipped to the kept letters.
  fixture.page("BT 4 Tr /F1 10 Tf 72 700 Td (Report on Zebulon today) Tj ET");
  const pdfkit::test::Redaction result = redact(fixture.bytes(), box("Zebulon"));
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.matches);
  const JsonValue occurrence = first_occurrence(result.matches);
  REQUIRE(occurrence.find("boxes")->as_array().size() == 1);
  const pdfkit::text::Rect drawn = rect_of(occurrence.find("boxes")->as_array().front());
  const auto painted = fills(result.pdf);
  REQUIRE(painted.size() == 1);
  CHECK_FALSE(painted.front().box.is_empty());
  CHECK(painted.front().box.width() == doctest::Approx(drawn.width()).epsilon(0.001));
  CHECK(painted.front().box.height() == doctest::Approx(drawn.height()).epsilon(0.001));
  CHECK(JsonReader::parse(result.matches).find("warnings")->as_array().empty());
}

TEST_CASE("a box match keeps its placeholder for the marked-content strings") {
  pdfkit::test::TextDocument fixture;
  fixture.page("/Span << /Alt (Zebulon) >> BDC BT /F1 10 Tf 72 700 Td (Zebulon) Tj ET EMC");
  const pdfkit::test::Redaction result =
      redact(fixture.bytes(), box("Zebulon", R"(, "placeholder": "[X]")"));
  REQUIRE(result.write_status == PDFKIT_OK);
  const auto pages = pdfkit::test::read_pages(result.pdf);
  REQUIRE(pages.front().content.spans.size() == 1);
  CHECK(pages.front().content.spans.front().alt == std::optional<std::string>("[X]"));
  CHECK(page_text(result.pdf).find("[X]") == std::string::npos);
  CHECK(fills(result.pdf).size() == 1);
}

TEST_CASE("a malformed style, colour or placeholder is refused with the three parts") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Zebulon) Tj ET");
  const std::string file = fixture.bytes();
  for (const std::string& bad : std::vector<std::string>{
           R"({"matches": [{"search": "Zebulon", "style": "blur"}]})",
           R"({"matches": [{"search": "Zebulon", "style": 1}]})",
           R"({"matches": [{"search": "Zebulon", "colour": [0, 0, 0]}]})",
           R"({"matches": [{"search": "Zebulon", "style": "text", "colour": [0, 0, 0]}]})",
           R"({"matches": [{"search": "Zebulon", "style": "box", "colour": [1.5, 0, 0]}]})",
           R"({"matches": [{"search": "Zebulon", "style": "box", "colour": [-0.1, 0, 0]}]})",
           R"({"matches": [{"search": "Zebulon", "style": "box", "colour": [0, 0]}]})",
           R"({"matches": [{"search": "Zebulon", "style": "box", "colour": "black"}]})",
           std::string(R"({"matches": [{"search": "Zebulon", "placeholder": "J)") + "\xC3" +
               R"(r"}]})"}) {
    INFO(bad);
    const pdfkit::test::Redaction result = redact(file, bad);
    CHECK(result.match_status == PDFKIT_E_INVALID_ARGUMENT);
    CHECK(result.matches.find("\"code\":\"invalid_argument\"") != std::string::npos);
    CHECK(result.matches.find("\"failed\"") != std::string::npos);
    CHECK(result.matches.find("\"expected\"") != std::string::npos);
    CHECK(result.matches.find("\"fix\"") != std::string::npos);
  }
}
