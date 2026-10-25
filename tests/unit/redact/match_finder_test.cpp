// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of redact::MatchFinder over pages read through the whole text layer:
// each folding flag on and off, a match in hidden text, a match across a
// hyphenation join, across a TJ number and a font change, the found ranges
// mapping back to the glyphs that drew the string, the text of a glyph range,
// and the overlap rule of docs/DESIGN.md section 11.

#include "redact/match_finder.hpp"

#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>
#include <vector>

namespace {

using pdfkit::redact::MatchFinder;
using pdfkit::test::TextDocument;
using pdfkit::text::PageText;

MatchFinder::Match search(const std::string& text, bool case_fold = true, bool diacritics = true,
                          bool whitespace = true, bool hyphenation = true) {
  MatchFinder::Match match;
  match.search = text;
  match.folding.text = {case_fold, diacritics, whitespace};
  match.folding.hyphenation = hyphenation;
  match.placeholder = "[REDACTED]";
  return match;
}

// The drawn text of the glyphs an occurrence covers.
std::string covered(const PageText& page, const MatchFinder::Occurrence& occurrence) {
  std::string out;
  for (std::size_t p = occurrence.first; p < occurrence.last; ++p) {
    out += page.content.glyphs[page.order[p]].text;
  }
  return out;
}

}  // namespace

TEST_CASE("a search folds case only when asked") {
  TextDocument document;
  document.page("BT /F1 10 Tf 72 700 Td (Report on JANE Doe and jane doe) Tj ET");
  const PageText page = document.read();
  MatchFinder finder(page);
  const auto folded = finder.find(search("Jane Doe"), 0);
  REQUIRE(folded.size() == 2);
  CHECK(covered(page, folded[0]) == "JANE Doe");
  CHECK(covered(page, folded[1]) == "jane doe");
  CHECK(finder.find(search("Jane Doe", false), 0).empty());
  CHECK(finder.find(search("jane doe", false), 0).size() == 1);
}

TEST_CASE("a search strips diacritics only when asked") {
  TextDocument document;
  // \311 is E-acute in WinAnsiEncoding.
  document.page("BT /F1 10 Tf 72 700 Td (Dear \\311lise, Elise) Tj ET");
  const PageText page = document.read();
  MatchFinder finder(page);
  CHECK(finder.find(search("elise"), 0).size() == 2);
  CHECK(finder.find(search("elise", true, false), 0).size() == 1);
  CHECK(finder.find(search("\xC3\x89lise", true, false), 0).size() == 1);
}

TEST_CASE("a search collapses white space and inferred gaps only when asked") {
  TextDocument document;
  document.page(
      "BT /F1 10 Tf 72 700 Td [(Jane) -400 (Doe)] TJ ET "
      "BT /F1 10 Tf 72 680 Td (Jane    Doe) Tj ET");
  const PageText page = document.read();
  MatchFinder finder(page);
  // The TJ gap is an inferred word gap and reads as a space.
  CHECK(finder.find(search("Jane Doe"), 0).size() == 2);
  CHECK(finder.find(search("Jane \n Doe"), 0).size() == 2);
  CHECK(finder.find(search("Jane \n Doe", true, true, false), 0).empty());
}

TEST_CASE("a match crosses a TJ number and a font change") {
  TextDocument document;
  document.page("BT /F1 10 Tf 72 700 Td [(Ja) -20 (n)] TJ /F1 12 Tf (e Doe) Tj ET");
  const PageText page = document.read();
  MatchFinder finder(page);
  const auto found = finder.find(search("jane doe"), 0);
  REQUIRE(found.size() == 1);
  CHECK(covered(page, found[0]) == "Jane Doe");
  CHECK(found[0].first == 0U);
  CHECK(found[0].last == 8U);
}

TEST_CASE("a match is found in hidden text") {
  TextDocument document;
  document.page(
      "BT /F1 10 Tf 72 700 Td (Visible) Tj ET "
      "BT 3 Tr /F1 10 Tf 72 600 Td (Secret Name) Tj ET");
  const PageText page = document.read();
  MatchFinder finder(page);
  const auto found = finder.find(search("secret name"), 0);
  REQUIRE(found.size() == 1);
  CHECK(found[0].first >= page.visible);
  CHECK(covered(page, found[0]) == "Secret Name");
}

TEST_CASE("a match crosses a hyphenation join and takes the hyphen") {
  TextDocument document;
  document.page("BT /F1 10 Tf 14 TL 72 700 Td (Zebulon Quarter-) Tj T* (field lives) Tj ET");
  const PageText page = document.read();
  MatchFinder finder(page);
  const auto found = finder.find(search("Zebulon Quarterfield"), 0);
  REQUIRE(found.size() == 1);
  CHECK(covered(page, found[0]) == "Zebulon Quarter-field");
  CHECK(finder.find(search("Zebulon Quarterfield", true, true, true, false), 0).empty());
  CHECK(finder.find(search("Quarter- field", true, true, true, false), 0).size() == 1);
}

TEST_CASE("the text of a glyph range is the extraction's text") {
  TextDocument document;
  document.page("BT /F1 10 Tf 14 TL 72 700 Td (Jane Doe) Tj T* (lives here) Tj ET");
  const PageText page = document.read();
  const MatchFinder finder(page);
  CHECK(finder.text(0, 8) == "Jane Doe");
  CHECK(finder.text(5, 13) == "Doe\nlives");
  CHECK(finder.text(0, 0).empty());
}

TEST_CASE("overlapping and adjacent occurrences merge by placeholder") {
  std::vector<MatchFinder::Occurrence> occurrences{
      {0, 1, 10, 20, "[A]"}, {1, 1, 15, 25, "[A]"}, {2, 1, 25, 30, "[A]"},
      {3, 1, 28, 40, "[B]"}, {4, 1, 50, 55, "[B]"}, {5, 1, 52, 53, "[C]"}};
  const auto merged = MatchFinder::merge(occurrences);
  REQUIRE(merged.size() == 3);
  CHECK(merged[0].first == 10U);
  CHECK(merged[0].last == 30U);
  CHECK(merged[0].placeholder == "[A]");
  CHECK(merged[1].first == 30U);
  CHECK(merged[1].last == 40U);
  CHECK(merged[1].placeholder == "[B]");
  CHECK(merged[2].first == 50U);
  CHECK(merged[2].last == 55U);
}

TEST_CASE("a string that folds to nothing finds nothing") {
  TextDocument document;
  document.page("BT /F1 10 Tf 72 700 Td (Jane Doe) Tj ET");
  const PageText page = document.read();
  MatchFinder finder(page);
  CHECK(finder.find(search("   "), 0).empty());
  CHECK(finder.find(search(""), 0).empty());
}
