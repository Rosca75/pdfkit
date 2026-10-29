// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The search folding of docs/DESIGN.md section 11 against the cases the
// Unicode data decides: the simple case folding leaves "ß" and "İ" alone and
// folds the final sigma, the diacritic folding strips the marks a canonical
// decomposition yields and keeps "Ł", which has none, and the whitespace
// folding collapses every run of white space to one space, each folded code
// point naming the source code point it came from.

#include "platform/unicode/folding.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::platform::unicode::Folding;

std::string fold(const std::string& text, bool case_fold, bool diacritics, bool whitespace) {
  return Folding::fold_utf8(text, {case_fold, diacritics, whitespace, {}, {}});
}

}  // namespace

TEST_CASE("the case folding is the simple one") {
  CHECK(fold("JANE Doe", true, false, false) == "jane doe");
  // U+00DF has only a full folding (to "ss"), so the simple folding keeps it.
  CHECK(fold("Straße", true, false, false) == "straße");
  CHECK(fold("STRASSE", true, false, false) != fold("Straße", true, false, false));
  // U+0130 folds only under the full and Turkic foldings.
  CHECK(fold("\xC4\xB0stanbul", true, false, false) == "\xC4\xB0stanbul");
  // The Greek final sigma folds to sigma; capital sigma too.
  CHECK(fold("\xCE\xA3\xCE\xBF\xCF\x82", true, false, false) == "\xCF\x83\xCE\xBF\xCF\x83");
  CHECK(Folding::simple_case_fold(U'ẞ') == U'ß');
  CHECK(fold("Jane", false, false, false) == "Jane");
}

TEST_CASE("the diacritic folding strips the marks a decomposition yields") {
  CHECK(fold("\xC3\x89lise", false, true, false) == "Elise");
  CHECK(fold("\xC3\x89lise", true, true, false) == "elise");
  // A decomposed input folds the same as a composed one.
  CHECK(fold("E\xCC\x81lise", true, true, false) == "elise");
  // U+0141 has no canonical decomposition, so it stays; o-acute and z-acute fold.
  CHECK(fold("\xC5\x81\xC3\xB3\x64\xC5\xBA", false, true, false) == "\xC5\x81odz");
  CHECK(fold("\xC5\x81\xC3\xB3\x64\xC5\xBA", true, true, false) == "\xC5\x82odz");
  // Under the diacritic folding U+0130 decomposes to I and a dot, then folds.
  CHECK(fold("\xC4\xB0", true, true, false) == "i");
  // The angstrom sign folds to a-ring, which decomposes.
  CHECK(fold("\xE2\x84\xAB", true, true, false) == "a");
  CHECK(Folding::is_mark(U'́'));
  CHECK_FALSE(Folding::is_mark(U'a'));
  CHECK(Folding::decompose(U'é') == std::u32string{U'e', U'́'});
  CHECK(Folding::decompose(U'x') == std::u32string{U'x'});
}

TEST_CASE("the whitespace folding collapses runs to one space") {
  CHECK(fold("Jane \t\n Doe", false, false, true) == "Jane Doe");
  CHECK(fold("Jane\xC2\xA0\xE2\x80\x83"
             "Doe",
             false, false, true) == "Jane Doe");
  CHECK(fold("Jane  Doe", false, false, false) == "Jane  Doe");
  CHECK(Folding::is_white_space(U'　'));
  CHECK(Folding::is_white_space(U'\u0085'));
  CHECK_FALSE(Folding::is_white_space(U'​'));
}

TEST_CASE("a positional presentation form reads as its letters under every folding") {
  // "Muhammad" in Arabic presentation forms B (initial meem, medial hah,
  // medial meem, final dal) reads as the base letters, whatever the options.
  const std::string shaped = "\xEF\xBB\xA3\xEF\xBA\xA4\xEF\xBB\xA4\xEF\xBA\xAA";
  const std::string letters = "\xD9\x85\xD8\xAD\xD9\x85\xD8\xAF";
  CHECK(fold(shaped, true, true, true) == letters);
  CHECK(fold(shaped, false, false, false) == letters);
  // The lam-alef ligature's final form is two letters, both naming it.
  const Folding::Mapped mapped =
      Folding::fold(Folding::decode("x\xEF\xBB\xBC"), {false, false, false, {}, {}});
  CHECK(Folding::encode(mapped.text) == "x\xD9\x84\xD8\xA7");
  REQUIRE(mapped.source.size() == 3);
  CHECK(mapped.source[1] == 1);
  CHECK(mapped.source[2] == 1);
  // Other compatibility forms stay what they are: a full-width letter.
  CHECK(fold("\xEF\xBC\xA1", true, true, true) == "\xEF\xBD\x81");
}

TEST_CASE("each folded code point names its source") {
  const Folding::Mapped mapped =
      Folding::fold(Folding::decode("A\xC3\x89  b"), {true, true, true, {}, {}});
  CHECK(Folding::encode(mapped.text) == "ae b");
  REQUIRE(mapped.source.size() == 4);
  CHECK(mapped.source[0] == 0);
  CHECK(mapped.source[1] == 1);
  CHECK(mapped.source[2] == 2);
  CHECK(mapped.source[3] == 4);
}

TEST_CASE("ill-formed UTF-8 reads as the replacement character") {
  CHECK(Folding::decode("a\xFF"
                        "b") == std::u32string{U'a', U'�', U'b'});
  CHECK(Folding::encode(std::u32string{U'a', static_cast<char32_t>(0xD800)}) == "a\xEF\xBF\xBD");
}
