// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of text::TextLayout over pages read through the whole text layer:
// words by gap, lines by baseline, two and three columns in reading order, a
// rotated block, a vertical (Identity-V) column, right-to-left lines drawn in
// visual and in logical order, a ruled table's cells as blocks, hyphenation
// both ways, a duplicate folded, an overprinted string kept whole, a string
// drawn later into a gap of another read where it stands, a string in
// another font run over the next word read as a word of its own while an
// accent in another font stays in its word, and order_is_stable: the
// glyphs given in ten shuffled orders lay out the same.

#include "text/text_layout.hpp"

#include "text/support/text_document.hpp"

#include <doctest.h>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace {

using pdfkit::test::TextDocument;
using pdfkit::text::PageText;
using pdfkit::text::TextLayout;

std::vector<std::string> words_of(const std::vector<TextLayout::Block>& blocks) {
  std::vector<std::string> out;
  for (const TextLayout::Block& block : blocks) {
    for (const TextLayout::Line& line : block.lines) {
      for (const TextLayout::Word& word : line.words) {
        out.push_back(word.text);
      }
    }
  }
  return out;
}

std::vector<std::string> block_texts(const PageText& page) {
  std::vector<std::string> out;
  out.reserve(page.blocks.size());
  for (const TextLayout::Block& block : page.blocks) {
    out.push_back(PageText::text_of({block}));
  }
  return out;
}

PageText read(const std::string& content, const std::string& resources = "") {
  TextDocument document;
  document.page(content, resources);
  return document.read();
}

// A column of lines at x, from y down by 14 pt.
std::string column(double x, double y, const std::vector<std::string>& lines) {
  std::string out = "BT /F1 10 Tf 14 TL " + std::to_string(x) + " " + std::to_string(y) + " Td ";
  for (const std::string& line : lines) {
    out += "(" + line + ") Tj T* ";
  }
  return out + "ET ";
}

}  // namespace

TEST_CASE("words are split by a gap wider than a tenth of the size and by spaces") {
  const PageText page = read(
      "BT /F1 10 Tf 72 700 Td [(Hel) -50 (lo) -300 (World)] TJ ET "
      "BT /F1 10 Tf 72 680 Td (two words) Tj ET");
  CHECK(words_of(page.blocks) == std::vector<std::string>{"Hello", "World", "two", "words"});
  CHECK(page.blocks.front().lines.front().words.front().first == 0U);
  CHECK(page.blocks.front().lines.front().words.front().last == 5U);
}

TEST_CASE("a line drawing space glyphs keeps its letter spacing inside words") {
  const PageText page = read("BT /F1 10 Tf 2 Tc 72 700 Td (spaced out) Tj ET");
  CHECK(words_of(page.blocks) == std::vector<std::string>{"spaced", "out"});
}

TEST_CASE("glyphs within a fifth of the size of one baseline form one line") {
  const PageText page = read(
      "BT /F1 10 Tf 72 700 Td (one) Tj ET BT /F1 10 Tf 100 701 Td (line) Tj ET "
      "BT /F1 10 Tf 72 680 Td (next) Tj ET");
  REQUIRE(page.blocks.size() == 1U);
  REQUIRE(page.blocks.front().lines.size() == 2U);
  CHECK(page.text() == "one line\nnext");
}

TEST_CASE("two and three columns read column by column") {
  const PageText two = read(column(72, 700, {"Left one", "Left two", "Left three"}) +
                            column(320, 700, {"Right one", "Right two", "Right three"}));
  CHECK(two.text() == "Left one\nLeft two\nLeft three\n\nRight one\nRight two\nRight three");
  const PageText three =
      read(column(400, 700, {"C one", "C two"}) + column(72, 700, {"A one", "A two"}) +
           column(240, 700, {"B one", "B two"}));
  CHECK(block_texts(three) ==
        std::vector<std::string>{"A one\nA two", "B one\nB two", "C one\nC two"});
}

TEST_CASE("a heading over two columns is read first") {
  const PageText page = read("BT /F1 14 Tf 72 740 Td (A heading across the page) Tj ET " +
                             column(72, 700, {"Left one", "Left two"}) +
                             column(320, 700, {"Right one", "Right two"}));
  CHECK(block_texts(page).front() == "A heading across the page");
  CHECK(page.text().find("Left two\n\nRight one") != std::string::npos);
}

TEST_CASE("rotated text forms lines along its rotation") {
  const PageText page = read(
      "BT /F1 10 Tf 0 1 -1 0 300 100 Tm (Rotated words) Tj "
      "0 1 -1 0 314 100 Tm (second line) Tj ET");
  REQUIRE(page.blocks.size() == 1U);
  REQUIRE(page.blocks.front().lines.size() == 2U);
  CHECK(page.blocks.front().lines.front().angle == doctest::Approx(90.0));
  CHECK(page.text() == "Rotated words\nsecond line");
}

TEST_CASE("an Identity-V column reads top to bottom") {
  const PageText page = read("BT /F3 10 Tf 300 700 Td <4E2D65874E2D> Tj ET");
  REQUIRE(page.blocks.size() == 1U);
  const TextLayout::Line& line = page.blocks.front().lines.front();
  CHECK(line.direction == "ttb");
  CHECK(page.text() == "\xE4\xB8\xAD\xE6\x96\x87\xE4\xB8\xAD");
}

TEST_CASE("a right-to-left line reads in logical order however it was drawn") {
  // Hebrew alef, bet, gimel: drawn in visual order (gimel at the left) and
  // in logical order (alef first, at the right).
  const PageText visual = read("BT /F2 10 Tf 72 700 Td <05D205D105D0> Tj ET");
  const PageText logical =
      read("BT /F2 10 Tf 92 700 Td <05D0> Tj -10 0 Td <05D1> Tj -10 0 Td <05D2> Tj ET");
  const std::string word = "\xD7\x90\xD7\x91\xD7\x92";
  CHECK(visual.text() == word);
  CHECK(logical.text() == word);
  CHECK(visual.blocks.front().lines.front().direction == "rtl");
}

TEST_CASE("a ruled table's cells are blocks read column by column") {
  const PageText page = read(
      "0 0 0 RG 70 600 400 100 re S 270 600 m 270 700 l S 70 650 m 470 650 l S "
      "BT /F1 10 Tf 80 680 Td (Name) Tj ET BT /F1 10 Tf 280 680 Td (Value) Tj ET "
      "BT /F1 10 Tf 80 630 Td (Alpha) Tj ET BT /F1 10 Tf 280 630 Td (Beta) Tj ET");
  CHECK(block_texts(page) == std::vector<std::string>{"Name", "Alpha", "Value", "Beta"});
}

TEST_CASE("a line-ending hyphen joins a lower-case start and not a capital") {
  const PageText page = read(column(72, 700, {"an exam-", "ple and Jean-", "Pierre came"}));
  const TextLayout::Block& block = page.blocks.front();
  REQUIRE(block.lines.size() == 3U);
  CHECK(block.lines[0].words.back().text == "exam-");
  CHECK(block.lines[0].words.back().hyphenated);
  CHECK(block.lines[1].words.back().text == "Jean-");
  CHECK_FALSE(block.lines[1].words.back().hyphenated);
}

TEST_CASE("a duplicate drawn at the same place is folded and read once") {
  const PageText page =
      read("BT /F1 10 Tf 72 700 Td (Bold) Tj ET BT /F1 10 Tf 72.3 700 Td (Bold) Tj ET");
  CHECK(page.text() == "Bold");
  REQUIRE(page.content.glyphs.size() == 8U);
  CHECK(page.content.glyphs[0].duplicates == std::vector<std::size_t>{4});
  CHECK(page.content.glyphs[4].duplicate_of == 0U);
  CHECK(page.content.glyphs[4].has(pdfkit::text::HiddenClass::duplicate));
  REQUIRE(page.hidden_blocks.size() == 1U);
}

TEST_CASE("a string drawn over another is read after it, not interleaved") {
  const PageText page =
      read("BT /F1 10 Tf 72 700 Td (Jane Doe) Tj ET BT /F1 10 Tf 72 700 Td (XXXXXXXX) Tj ET");
  CHECK(page.text() == "Jane Doe XXXXXXXX");
}

TEST_CASE("a string drawn later into a gap of another reads where it stands") {
  // A form's labels drawn first, its value drawn afterwards into the space
  // left between them; then its two values in one text object, each into
  // the space after its label.
  const PageText one = read(
      "BT /F1 10 Tf 72 700 Td (Name:) Tj 100 0 Td (Date:) Tj ET "
      "BT /F1 10 Tf 110 700 Td (Jane Doe) Tj ET");
  CHECK(one.text() == "Name: Jane Doe Date:");
  const PageText two = read(
      "BT /F1 10 Tf 72 700 Td (Name:) Tj 100 0 Td (Date:) Tj ET "
      "BT /F1 10 Tf 110 700 Td (Jane Doe) Tj 100 0 Td (2026) Tj ET");
  CHECK(two.text() == "Name: Jane Doe Date: 2026");
}

TEST_CASE("a string in another font that runs over the next word is a word of its own") {
  // "Huisman" in Times runs 6 pt over "today.", drawn after it in
  // Helvetica, and a mask of asterisks 3 pt over it.
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>";
  for (const auto& [over, start] :
       std::vector<std::pair<std::string, std::string>>{{"Huisman", "150"}, {"*****", "142"}}) {
    INFO(over);
    TextDocument document;
    document.page("BT /F1 10 Tf 72 700 Td (Signed by) Tj ET BT /F4 10 Tf 120 700 Td (" + over +
                      ") Tj ET BT /F1 10 Tf " + start + " 700 Td (today.) Tj ET",
                  "", "", times);
    CHECK(document.read().text() == "Signed by " + over + " today.");
  }
}

TEST_CASE("an accent drawn in another font over its letter stays in the word") {
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>";
  TextDocument document;
  document.page("BT /F1 10 Tf 72 700 Td (cafe) Tj ET BT /F4 10 Tf 87 700 Td (\\264) Tj ET", "", "",
                times);
  CHECK(document.read().text() == "cafe\xC2\xB4");
}

TEST_CASE("order_is_stable") {
  TextDocument document;
  document.page(column(72, 700, {"The quick brown", "fox jumps over", "the lazy dog"}) +
                column(320, 700, {"Second column", "of text here"}) +
                "BT /F1 10 Tf 72 700 Td (The) Tj ET");
  const PageText page = document.read();
  std::vector<std::size_t> members;
  members.reserve(page.content.glyphs.size());
  for (const auto& glyph : page.content.glyphs) {
    members.push_back(glyph.draw);
  }
  const TextLayout::Result reference = TextLayout::run(page.content.glyphs, members, 0);
  for (unsigned seed = 1; seed <= 10; ++seed) {
    std::vector<std::size_t> shuffled = members;
    std::mt19937 generator(seed);
    std::shuffle(shuffled.begin(), shuffled.end(), generator);
    const TextLayout::Result result = TextLayout::run(page.content.glyphs, shuffled, 0);
    CHECK(result.order == reference.order);
    CHECK(words_of(result.blocks) == words_of(reference.blocks));
  }
}
