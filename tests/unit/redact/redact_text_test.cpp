// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Text redaction through the C ABI, end to end: the matched text leaves the
// output's text and bytes, every other glyph is read back at its place, the
// placeholder is read where the match was, the write report names the
// rectangles and the self-check's pass; the cases a producer writes (two
// /Contents pieces, a two-byte font, hidden text, a fake-bold duplicate, a
// page selection) and the refusals of a glyph range (another document's
// fingerprint, a range past its page) and of malformed matches.

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
  const std::size_t at = probe.matches.find("\"fingerprint\":\"");
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
