// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// A search that does not depend on one reading order, docs/DESIGN.md
// sections 11 and 18: a phrase another reader exposes in its own order is
// found by MatchFinder::find_orders, refused by the self-check when it
// survives, and removed by a text redaction, whose output then passes the
// self-check and reads the phrase in no order. Each fixture is a page whose
// reading order splits the phrase while another order a reader uses keeps it
// whole: two blocks whose lines interleave (the drawing order reads each
// block whole), two columns side by side (a reader that keeps the page's
// rows reads across the column gap), a right-to-left word drawn in visual
// order and one drawn in logical order (a reader that reads each baseline
// left to right or right to left), a line in a composite font without
// widths that runs off the page (the drawing order reads it whole) and the
// TeX logo, whose lowered E sits on a baseline of its own.

#include "platform/byte_span.hpp"
#include "redact/match_finder.hpp"
#include "redact/self_check.hpp"
#include "redact/support/redaction.hpp"
#include "syntax/limits.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using pdfkit::redact::MatchFinder;
using pdfkit::redact::SelfCheck;
using pdfkit::test::TextDocument;
using pdfkit::text::PageText;

MatchFinder::Match search(std::string_view text) {
  MatchFinder::Match match;
  match.search = std::string(text);
  match.placeholder = "[REDACTED]";
  return match;
}

// The drawn text of the glyphs the occurrences cover, in reading order.
std::string covered(const PageText& page, const std::vector<MatchFinder::Occurrence>& found) {
  std::string out;
  for (const MatchFinder::Occurrence& occurrence : found) {
    for (std::size_t p = occurrence.first; p < occurrence.last; ++p) {
      out += page.content.glyphs[page.order[p]].text;
    }
  }
  return out;
}

// Two blocks of 14 pt lines drawn one after the other, the second 12 pt
// below the first, so the reading order interleaves their lines.
std::string_view interleaved() {
  return "BT /F1 14 Tf 72 700 Td (Alpha Bravo) Tj 0 -24 Td (Charlie Delta) Tj 0 -24 Td (Echo) Tj "
         "ET BT /F1 14 Tf 74 688 Td (Golf Hotel) Tj 0 -24 Td (India Juliet) Tj 0 -24 Td (Kilo) "
         "Tj ET";
}

// Two columns side by side; "Annual" ends the first column's first line and
// "Report" starts the second's, on the same baseline.
std::string_view columns() {
  return "BT /F1 10 Tf 72 700 Td (Quarterly Annual) Tj 0 -12 Td (more text here) Tj 0 -12 Td "
         "(and here too) Tj ET BT /F1 10 Tf 320 700 Td (Report follows) Tj 0 -12 Td "
         "(second column) Tj 0 -12 Td (third line) Tj ET";
}

// The Hebrew word "shalom" (U+05E9 U+05DC U+05D5 U+05DD) in the identity
// font F2: drawn in visual order (its last letter leftmost, the codes left
// to right) on the first line, and in logical order with each letter placed
// to the left of the one before on the second.
std::string_view hebrew() {
  return "BT /F2 10 Tf 72 700 Td <05DD05D505DC05E9> Tj ET "
         "BT /F2 10 Tf 300 650 Td <05E9> Tj -10 0 Td <05DC> Tj -10 0 Td <05D5> Tj -10 0 Td "
         "<05DD> Tj ET";
}

std::string_view shalom_logical() {
  return "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
}
std::string_view shalom_visual() {
  return "\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9";
}

// "Toshiba Service Manual" in the identity font F2, whose Latin letters have
// no width of their own (the default 1000), from x = 500: the line runs past
// the page's right edge at 612, so its tail is off the media.
std::string_view widthless() {
  return "BT /F2 12 Tf 500 700 Td "
         "<0054006F00730068006900620061002000530065007200760069006300650020004D0061006E00750061006C"
         ">"
         " Tj ET BT /F1 10 Tf 72 600 Td (other text) Tj ET";
}

// The TeX logo: T, an E lowered by 0.22 of the size and kerned back, an X.
std::string_view tex_logo() {
  return "BT /F1 10 Tf 72 700 Td (T) Tj 4.44 -2.2 Td (E) Tj 4.9 2.2 Td (X) Tj 12 0 Td (logo) Tj "
         "ET";
}

PageText page_of(TextDocument& document, std::string_view content) {
  document.page(std::string(content));
  return document.read();
}

bool survives(const std::string& file, const std::string& phrase) {
  const std::vector<MatchFinder::Match> needles{search(phrase)};
  const SelfCheck::Result result =
      SelfCheck::run(pdfkit::platform::as_bytes(file), pdfkit::syntax::Limits{}, needles, {});
  for (const SelfCheck::Survivor& survivor : result.survivors) {
    if (survivor.surface == "text") {
      return true;
    }
  }
  return false;
}

// Whether any order of any page of output reads phrase.
bool read_anywhere(const std::string& output, const std::string& phrase) {
  for (const PageText& page : pdfkit::test::read_pages(output)) {
    MatchFinder finder(page);
    if (!finder.find(search(phrase), 0).empty() || !finder.find_orders(search(phrase), 0).empty()) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("a phrase across interleaved lines is found in the drawing order") {
  TextDocument document;
  const PageText page = page_of(document, interleaved());
  MatchFinder finder(page);
  REQUIRE(finder.find(search("Bravo Charlie"), 0).empty());
  const auto found = finder.find_orders(search("Bravo Charlie"), 0);
  REQUIRE_FALSE(found.empty());
  CHECK(covered(page, found) == "BravoCharlie");
  CHECK(found.front().placeholder == "[REDACTED]");
  for (std::size_t i = 1; i < found.size(); ++i) {
    CHECK(found[i].placeholder.empty());
  }
  // A phrase the reading order already finds is not found again.
  CHECK_FALSE(finder.find(search("Alpha Bravo"), 0).empty());
  CHECK(finder.find_orders(search("Alpha Bravo"), 0).empty());
  // Words that no order puts side by side are not a phrase.
  CHECK(finder.find_orders(search("Alpha Kilo"), 0).empty());
}

TEST_CASE("a phrase across a column gap is found along the baseline") {
  TextDocument document;
  const PageText page = page_of(document, columns());
  MatchFinder finder(page);
  REQUIRE(finder.find(search("Annual Report"), 0).empty());
  const auto found = finder.find_orders(search("Annual Report"), 0);
  REQUIRE_FALSE(found.empty());
  CHECK(covered(page, found) == "AnnualReport");
  // The gap is a word gap: a word is never read across it.
  CHECK(finder.find_orders(search("AnnualReport"), 0).empty());
  CHECK(finder.find_orders(search("nualRep"), 0).empty());
}

TEST_CASE("a right-to-left word is found read along its baseline either way") {
  TextDocument document;
  const PageText page = page_of(document, hebrew());
  MatchFinder finder(page);
  // The reading order reads both lines logically.
  CHECK(finder.find(search(shalom_logical()), 0).size() == 2);
  REQUIRE(finder.find(search(shalom_visual()), 0).empty());
  // A reader that reads each baseline left to right reads both visually.
  const auto found = finder.find_orders(search(shalom_visual()), 0);
  CHECK(covered(page, found).size() == 2 * shalom_visual().size());
}

TEST_CASE("a line without widths that runs off the page is found as drawn") {
  TextDocument document;
  const PageText page = page_of(document, widthless());
  MatchFinder finder(page);
  REQUIRE(finder.find(search("Toshiba Service Manual"), 0).empty());
  const auto found = finder.find_orders(search("Toshiba Service Manual"), 0);
  REQUIRE_FALSE(found.empty());
  CHECK(covered(page, found) == "Toshiba Service Manual");
  CHECK(finder.find_orders(search("other text"), 0).empty());
}

TEST_CASE("the TeX logo is found as drawn") {
  TextDocument document;
  const PageText page = page_of(document, tex_logo());
  MatchFinder finder(page);
  REQUIRE(finder.find(search("TeX"), 0).empty());
  const auto found = finder.find_orders(search("TeX"), 0);
  REQUIRE_FALSE(found.empty());
  std::string letters = covered(page, found);
  CHECK(letters.size() == 3);
  CHECK(letters.find('E') != std::string::npos);
}

TEST_CASE("the self-check refuses a phrase another reader's order exposes") {
  const std::vector<std::pair<std::string, std::string>> cases{
      {std::string(interleaved()), "Bravo Charlie"},
      {std::string(columns()), "Annual Report"},
      {std::string(hebrew()), std::string(shalom_visual())},
      {std::string(widthless()), "Toshiba Service Manual"},
      {std::string(tex_logo()), "TeX"}};
  for (const auto& [content, phrase] : cases) {
    INFO(phrase);
    TextDocument document;
    document.page(content);
    CHECK(survives(document.bytes(), phrase));
  }
  TextDocument clean;
  clean.page(std::string(interleaved()));
  CHECK_FALSE(survives(clean.bytes(), "Alpha Kilo"));
}

TEST_CASE("the placeholder's own letters never spell a survivor in another order") {
  // Squeezed into the holes of "Dies" and "Test", the placeholders' letters
  // lie among the kept words' along the baseline; "TE" of a placeholder and
  // "st" of "ist" read "TEst" in a row, which is no glyph of the document.
  TextDocument document;
  document.page("BT /F4 12 Tf 70.8 760.3 Td (Dies ist ein Test f\\374r Attachments!) Tj ET", "", "",
                "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman "
                "/Encoding /WinAnsiEncoding >>");
  const pdfkit::test::Redaction result = pdfkit::test::redact(
      document.bytes(),
      R"({"matches": [{"search": "Seite"}, {"search": "Test"}, {"search": "Attachment"},
                      {"search": "Attachments"}, {"search": "Dies"}]})");
  REQUIRE(result.match_status == PDFKIT_OK);
  INFO(result.report);
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(pdfkit::test::page_text(result.pdf).find("ist ein") != std::string::npos);
}

TEST_CASE("a redaction removes a phrase from every order and passes the self-check") {
  const std::vector<std::tuple<std::string, std::string, std::string>> cases{
      {std::string(interleaved()), "Bravo Charlie", "Golf Hotel"},
      {std::string(columns()), "Annual Report", "second column"},
      {std::string(hebrew()), std::string(shalom_visual()), ""},
      {std::string(widthless()), "Toshiba Service Manual", "other text"},
      {std::string(tex_logo()), "TeX", "logo"}};
  for (const auto& [content, phrase, kept] : cases) {
    INFO(phrase);
    TextDocument document;
    document.page(content);
    const pdfkit::test::Redaction result =
        pdfkit::test::redact(document.bytes(), pdfkit::test::search(phrase));
    REQUIRE(result.match_status == PDFKIT_OK);
    INFO(result.matches);
    REQUIRE(result.write_status == PDFKIT_OK);
    CHECK(result.report.find("\"self_check\":{\"status\":\"passed\"") != std::string::npos);
    CHECK_FALSE(read_anywhere(result.pdf, phrase));
    const std::string text = pdfkit::test::page_text(result.pdf);
    CHECK(text.find("[REDACTED]") != std::string::npos);
    if (!kept.empty()) {
      CHECK(text.find(kept) != std::string::npos);
    }
  }
}
