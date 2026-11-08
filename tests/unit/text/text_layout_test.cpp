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
// accent in another font stays in its word, a word set apart as a string of
// its own on a line drawing spaces, two lines of large glyphs drawn across
// each other, a vertical word with gaps inside it, a space taken back under
// the next letter, a glyph mapped to a right-to-left word and a letter, a
// Type 3 font far larger than its size, and order_is_stable: the glyphs
// given in ten shuffled orders lay out the same.

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

TEST_CASE(
    "a word a producer sets apart in an operator of its own is a word on a line drawing spaces") {
  // dccc-0124: a justified column draws its spaces, but sets "anfuhr" in a
  // TJ of its own 0.2 of the size after "Jahre", which ends the TJ before.
  const PageText page =
      read("BT /F1 10 Tf 100 700 Td (viele Jahre) Tj 1 0 0 1 150.35 700 Tm (anfuhr) Tj ET");
  CHECK(words_of(page.blocks) == std::vector<std::string>{"viele", "Jahre", "anfuhr"});
  // Two strings of one TJ array keep the spaced gap, so letter spacing, a
  // letter-spaced heading and a number a TJ number spaces stay whole
  // (pdfjs-0016's "Ideas:" "The", 0.2 apart in one TJ, stays glued with
  // them).
  for (const auto& [line, words] : std::vector<std::pair<std::string, std::vector<std::string>>>{
           {"[(Sp) -200 (aced out)]", {"Spaced", "out"}},
           {"[(T) -200 (R) -200 (AV) -180 (EL) ( now)]", {"TRAVEL", "now"}},
           {"[(3.) -200 (14 is pi)]", {"3.14", "is", "pi"}},
           {"[(New Ideas:) -200 (The end)]", {"New", "Ideas:The", "end"}},
           {"[(a) -200 (b) -200 (c) ( d)]", {"abc", "d"}}}) {
    INFO(line);
    CHECK(words_of(read("BT /F1 10 Tf 100 700 Td " + line + " TJ ET").blocks) == words);
  }
  // Two operators 0.16 of the size apart are two words, whatever the
  // producer meant: "a Bo" and "ld word" read "Bo" "ld", the cost of reading
  // dccc-0124's "in" "Hermeskeil", 0.158 apart, as two words.
  const PageText bold =
      read("BT /F1 10 Tf 100 700 Td (a Bo) Tj 1 0 0 1 122.17 700 Tm (ld word) Tj ET");
  CHECK(words_of(bold.blocks) == std::vector<std::string>{"a", "Bo", "ld", "word"});
}

TEST_CASE("glyphs within a fifth of the size of one baseline form one line") {
  const PageText page = read(
      "BT /F1 10 Tf 72 700 Td (one) Tj ET BT /F1 10 Tf 100 701 Td (line) Tj ET "
      "BT /F1 10 Tf 72 680 Td (next) Tj ET");
  REQUIRE(page.blocks.size() == 1U);
  REQUIRE(page.blocks.front().lines.size() == 2U);
  CHECK(page.text() == "one line\nnext");
}

TEST_CASE("two runs of large glyphs on baselines a fifth of their size apart are two lines") {
  // pdfdiff-0018: "B 8" in Times-Bold at 500 pt on the baseline 90 and "8B"
  // in Helvetica at 660 pt on the baseline 10, drawn over each other.
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Bold /Encoding /WinAnsiEncoding >>";
  TextDocument document;
  document.page("BT /F4 500 Tf 1 0 0 1 10 90 Tm (B 8) Tj /F1 660 Tf 1 0 0 1 10 10 Tm (8B) Tj ET",
                "", "", times);
  // The two lines are read top edge first, as every block's lines are.
  CHECK(document.read().text() == "8B\nB 8");
}

TEST_CASE("subscripts drawn across their line's letters stay in the line") {
  // dccc-0128's "m1 x m2": the subscripts "1" and "2" at 7 pt, one run 2 pt
  // below the 10 pt "m x m", each over a letter of it and it over them.
  const PageText page =
      read("BT /F1 10 Tf 100 700 Td (m\327m) Tj ET BT /F1 7 Tf 109 698 Td [(1) -444 (2)] TJ ET");
  REQUIRE(page.blocks.size() == 1U);
  CHECK(page.blocks.front().lines.size() == 1U);
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

TEST_CASE("a vertical word keeps the gaps its TJ numbers leave inside it") {
  // pdfium-0049: "Hello" set vertically at 12 pt in full-em cells, its TJ
  // numbers leaving gaps of 0.1 and 0.2 em inside the word.
  const PageText page =
      read("BT /F3 12 Tf 110 190 Td [<0048> 100 <0065> -100 <006c006c> 200 <006f>] TJ ET");
  CHECK(words_of(page.blocks) == std::vector<std::string>{"Hello"});
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

TEST_CASE("a space a TJ number takes back under the next letter does not split its word") {
  // pdfjs-0018: a Hebrew line drawn in visual order from its left end opens
  // with a space, and a TJ number moves the word's last letter back over
  // it and 0.12 pt past it: the word reads whole, its space after it in
  // logical order.
  const PageText page =
      read("BT /F2 10 Tf 100 700 Td [<0020> 262 <05D405E005D905D305DE05D4> -500 <05DC05E9>] TJ ET");
  CHECK(words_of(page.blocks) ==
        std::vector<std::string>{"\xD7\xA9\xD7\x9C",
                                 "\xD7\x94\xD7\x9E\xD7\x93\xD7\x99\xD7\xA0\xD7\x94"});
}

TEST_CASE(
    "a glyph mapped to a right-to-left word, a space and a letter joins the letters after it") {
  // pypdf-0019: the /ToUnicode of the "h" of "habibi" holds the Arabic word
  // before it, a space and the "h"; the line also holds a right-to-left
  // word of six letters, so it reads right to left.
  TextDocument document;
  const auto map = document.add_stream(
      "",
      "/CIDInit /ProcSet findresource begin 12 dict begin begincmap 1 begincodespacerange "
      "<0000> <FFFF> endcodespacerange 10 beginbfchar <0068> <05D005D100200068> <0061> "
      "<0061> <0062> <0062> <0069> <0069> <0020> <0020> <05D4> <05D4> <05E0> <05E0> <05D9> "
      "<05D9> <05D3> <05D3> <05DE> <05DE> endbfchar endcmap CMapName currentdict /CMap "
      "defineresource pop end end");
  const std::string cluster =
      "/F8 << /Type /Font /Subtype /Type0 /BaseFont /TestCID /Encoding /Identity-H /ToUnicode " +
      std::to_string(map) +
      " 0 R /DescendantFonts [<< /Type /Font /Subtype /CIDFontType2 /BaseFont /TestCID "
      "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /DW 500 >>] >>";
  document.page(
      "BT /F8 10 Tf 100 700 Td <006800610062006900620069002005D405E005D905D305DE05D4> Tj ET", "",
      "", cluster);
  const std::vector<std::string> words = words_of(document.read().blocks);
  CHECK(std::ranges::any_of(words,
                            [](const std::string& word) { return word.ends_with(" habibi"); }));
  CHECK(std::ranges::find(words, "\xD7\x94\xD7\x9E\xD7\x93\xD7\x99\xD7\xA0\xD7\x94") !=
        words.end());
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
    std::string content = "BT /F1 10 Tf 72 700 Td (Signed by) Tj ET BT /F4 10 Tf 120 700 Td (";
    content += over;
    content += ") Tj ET BT /F1 10 Tf ";
    content += start;
    content += " 700 Td (today.) Tj ET";
    TextDocument document;
    document.page(content, "", "", times);
    std::string expected = "Signed by ";
    expected += over;
    expected += " today.";
    CHECK(document.read().text() == expected);
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

TEST_CASE("letters of a font whose spaces another font draws keep their letter spacing") {
  // Letters in Times set 0.15 of the size apart by TJ adjustments, the
  // spaces between the words drawn in Helvetica.
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>";
  TextDocument document;
  document.page(
      "BT /F4 10 Tf 72 700 Td [(R) -150 (e) -150 (c) -150 (u) -150 (e) -150 (i) -150 (l)] TJ "
      "/F1 10 Tf ( ) Tj /F4 10 Tf [(d) -150 (e)] TJ /F1 10 Tf ( ) Tj "
      "/F4 10 Tf [(l) -150 (a)] TJ ET",
      "", "", times);
  CHECK(document.read().text() == "Recueil de la");
}

TEST_CASE("a word alone on its line keeps its letter spacing before a space in another font") {
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>";
  TextDocument document;
  document.page(
      "BT /F4 10 Tf 72 700 Td [(c) -150 (o) -150 (n) -150 (t) -150 (r) -150 (e)] TJ "
      "/F1 10 Tf ( ) Tj ET",
      "", "", times);
  CHECK(document.read().text() == "contre");
}

TEST_CASE("spaces drawn apart from a line's words leave its words set by position") {
  // Three words set 0.2 of the size apart without space glyphs, a line
  // further down, then a string " - " placed just after the last word.
  const PageText page = read(
      "BT /F1 10 Tf 72 700 Td [(Lord) -200 (from) -200 (heav)] TJ ET "
      "BT /F1 10 Tf 72 600 Td (below) Tj ET "
      "BT /F1 10 Tf 137.69 700 Td ( - ) Tj ET");
  const std::vector<std::string> words = words_of(page.blocks);
  REQUIRE(words.size() >= 3U);
  CHECK(std::vector<std::string>(words.begin(), words.begin() + 3) ==
        std::vector<std::string>{"Lord", "from", "heav"});
}

TEST_CASE("a space a string in another font runs over stays with that string") {
  // "Upconversion", then a space set inside its "e" and "How" in Times drawn
  // over that space.
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>";
  TextDocument document;
  document.page(
      "BT /F1 10 Tf 72 700 Td (Upconversion) Tj ET "
      "BT /F1 10 Tf 105 700 Td ( ) Tj /F4 10 Tf [250 (How)] TJ ET",
      "", "", times);
  const std::vector<std::string> words = words_of(document.read().blocks);
  CHECK(std::ranges::find(words, "Upconversion") != words.end());
}

TEST_CASE("a space drawn earlier inside a later word does not split it") {
  // "Left" and a space set at 270, inside the advance of the "h" of "South"
  // drawn afterwards from 250 (its "t" ends at 270.57 where the "h" starts).
  const PageText page = read(
      "BT /F1 10 Tf 72 700 Td (Left) Tj 198 0 Td ( ) Tj ET "
      "BT /F1 10 Tf 250 700 Td (South) Tj ET");
  const std::vector<std::string> words = words_of(page.blocks);
  CHECK(std::ranges::find(words, "South") != words.end());
}

TEST_CASE("two subsets of one face are one font") {
  // "Fo" and "rum" in two subsets of one face, the second kerned 0.15 of the
  // size back under the first.
  std::string widths;
  for (int c = 32; c <= 126; ++c) {
    widths += "500 ";
  }
  const auto subset = [&](const std::string& key, const std::string& tag) {
    return "/" + key + " << /Type /Font /Subtype /Type1 /BaseFont /" + tag +
           "+Helvetica /FirstChar 32 /LastChar 126 /Widths [" + widths +
           "] /Encoding /WinAnsiEncoding >>";
  };
  TextDocument document;
  document.page("BT /F5 10 Tf 72 700 Td (Fo) Tj /F6 10 Tf [150 (rum)] TJ ET", "", "",
                subset("F5", "ABCDEF") + " " + subset("F6", "GHIJKL"));
  CHECK(document.read().text() == "Forum");
}

TEST_CASE("a glyph from a second encoding of one face stays in the word") {
  // "C" and "591" in Helvetica, the hyphen between them from the same face
  // stated again as Helvetica+20, 0.15 of the size after the "C".
  std::string widths;
  for (int c = 32; c <= 126; ++c) {
    widths += "500 ";
  }
  const auto font = [&](const std::string& key, const std::string& name) {
    return "/" + key + " << /Type /Font /Subtype /Type1 /BaseFont /" + name +
           " /FirstChar 32 /LastChar 126 /Widths [" + widths + "] /Encoding /WinAnsiEncoding >>";
  };
  TextDocument document;
  document.page(
      "BT /F5 10 Tf 72 700 Td (affaire ) Tj (C) Tj /F6 10 Tf [-150 (-)] TJ /F5 10 Tf (591) Tj ET",
      "", "", font("F5", "Helvetica") + " " + font("F6", "Helvetica+20"));
  CHECK(document.read().text() == "affaire C-591");
}

TEST_CASE("a glyph whose spacing a TJ number takes back is not drawn over by the next") {
  // A "2" shown with 2.7 of character spacing, taken back by a TJ number, so
  // the "m" in Times starts where the "2"'s width ends.
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>";
  TextDocument document;
  document.page("BT /F1 10 Tf 2.7 Tc 72 700 Td (2) Tj /F4 10 Tf 0 Tc [270 (m)] TJ ET", "", "",
                times);
  CHECK(document.read().text() == "2m");
}

TEST_CASE("a subscript in another font tucked under its letter stays in the word") {
  // "k" in Helvetica ends at 77; the "2" in Times at 7 pt starts 1.5 pt
  // before that, 1.5 pt below the baseline.
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>";
  TextDocument document;
  document.page("BT /F1 10 Tf 72 700 Td (k) Tj ET BT /F4 7 Tf 75.5 698.5 Td (2) Tj ET", "", "",
                times);
  CHECK(document.read().text() == "k2");
}

TEST_CASE("a glyph drawn twice in two faces at one place is not a word of its own") {
  // A page number drawn in Helvetica, then again in Times 0.18 pt beside
  // it (a bold made by two faces).
  const std::string times =
      "/F4 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>";
  TextDocument document;
  document.page("BT /F1 12 Tf 302.66 20.38 Td (4) Tj ET BT /F4 12 Tf 302.48 20.2 Td (4) Tj ET", "",
                "", times);
  CHECK(words_of(document.read().blocks).size() == 1U);
}

TEST_CASE("a Type 3 font whose matrix makes its glyphs far larger than its size reads its words") {
  // pdfjs-0047: a Type 3 font with /FontMatrix [1 0 0 1 0 0] shown at 0.24,
  // its glyphs 72 units tall (17 pt) with gaps under 1 pt inside words and
  // 4.6 pt between them; each piece placed by its own text matrix.
  const std::string type3 =
      "/F7 << /Type /Font /Subtype /Type3 /FontBBox [0 -14 72 58] /FontMatrix [1 0 0 1 0 0] "
      "/CharProcs << >> /Resources << >> /FirstChar 73 /LastChar 121 /Widths [16 0 0 0 0 0 0 37 0 "
      "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 31 0 31 0 31 0 0 0 0 0 0 0 0 34 34 34 0 22 31 19 34 0 0 0 31] "
      "/Encoding << /Type /Encoding /Differences [73 /I 80 /P 97 /a 99 /c 101 /e 110 /n 111 /o 112 "
      "/p 114 /r 115 /s 116 /t 117 /u 121 /y] >> >>";
  std::string content = "BT /F7 0.24 Tf ";
  const std::vector<std::pair<double, std::string>> pieces = {
      {192.24, "P"}, {201.84, "ro"}, {215.52, "p"},   {223.92, "e"},   {232.32, "r"},
      {237.6, "t"},  {241.92, "y"},  {253.92, "Ins"}, {273.84, "ura"}, {295.68, "nce"}};
  for (const auto& [x, piece] : pieces) {
    content += "1 0 0 1 " + std::to_string(x) + " 738.48 Tm [(" + piece + ")] TJ ";
  }
  TextDocument document;
  document.page(content + "ET", "", "", type3);
  CHECK(document.read().text() == "Property Insurance");
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
