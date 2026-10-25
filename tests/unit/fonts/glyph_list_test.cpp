// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of fonts::GlyphList and fonts::Ligatures: Adobe Glyph List names,
// the Zapf Dingbats list, every naming convention (uniXXXX and its sequences,
// uXXXX to uXXXXXX, the period suffix, the underscore ligature), the
// number-bearing names that give no text, a name nothing answers, the
// unusable code points, and the ligature decompositions.

#include "fonts/glyph_list.hpp"

#include "fonts/ligatures.hpp"
#include "support/require_value.hpp"

#include <doctest.h>

#include <optional>
#include <string>

namespace {

using pdfkit::fonts::GlyphList;
using pdfkit::fonts::Ligatures;
using pdfkit::test::require_value;

TEST_CASE("Adobe Glyph List names decode to their characters") {
  CHECK(GlyphList::to_unicode("A") == std::optional<std::string>("A"));
  CHECK(GlyphList::to_unicode("eacute") == std::optional<std::string>("\xC3\xA9"));
  CHECK(GlyphList::to_unicode("Euro") == std::optional<std::string>("\xE2\x82\xAC"));
  CHECK(GlyphList::to_unicode("quoteright") == std::optional<std::string>("\xE2\x80\x99"));
  // A name of the list that stands for two code points.
  CHECK(GlyphList::to_unicode("dalethatafpatah") == std::optional<std::string>("\xD7\x93\xD6\xB2"));
  // The Zapf Dingbats list.
  CHECK(GlyphList::to_unicode("a1") == std::optional<std::string>("\xE2\x9C\x81"));
}

TEST_CASE("uni names, alone and in sequences of four-digit groups") {
  CHECK(GlyphList::to_unicode("uni0041") == std::optional<std::string>("A"));
  CHECK(GlyphList::to_unicode("uni00410042") == std::optional<std::string>("AB"));
  CHECK(GlyphList::to_unicode("uni00e9") == std::optional<std::string>("\xC3\xA9"));
  // A surrogate is not a character.
  CHECK_FALSE(GlyphList::to_unicode("uniD800").has_value());
  // A group that is not four hexadecimal digits.
  CHECK_FALSE(GlyphList::to_unicode("uni00G1").has_value());
}

TEST_CASE("u names of four to six upper-case hexadecimal digits") {
  CHECK(GlyphList::to_unicode("u0041") == std::optional<std::string>("A"));
  CHECK(GlyphList::to_unicode("u1F600") == std::optional<std::string>("\xF0\x9F\x98\x80"));
  CHECK_FALSE(GlyphList::to_unicode("u1234567").has_value());
  CHECK_FALSE(GlyphList::to_unicode("u00e9").has_value());
}

TEST_CASE("a suffix after a period is dropped") {
  CHECK(GlyphList::to_unicode("a.sc") == std::optional<std::string>("a"));
  CHECK(GlyphList::to_unicode("one.oldstyle") == std::optional<std::string>("1"));
  CHECK(GlyphList::to_unicode("uni0041.alt") == std::optional<std::string>("A"));
  CHECK_FALSE(GlyphList::to_unicode(".notdef").has_value());
}

TEST_CASE("an underscore joins the parts of a ligature") {
  CHECK(GlyphList::to_unicode("f_f_i") == std::optional<std::string>("ffi"));
  CHECK(GlyphList::to_unicode("T_h") == std::optional<std::string>("Th"));
  CHECK(GlyphList::to_unicode("f_uni0069") == std::optional<std::string>("fi"));
  CHECK_FALSE(GlyphList::to_unicode("f_").has_value());
  CHECK_FALSE(GlyphList::to_unicode("f_nonsense").has_value());
}

TEST_CASE("names that count glyphs, CIDs or codes give no text, only their number") {
  for (const char* name : {"g12", "glyph12", "G12", "cid12", "C12"}) {
    CHECK_FALSE(GlyphList::to_unicode(name).has_value());
  }
  const GlyphList::Number glyph = require_value(GlyphList::number("g12"));
  CHECK(glyph.kind == GlyphList::Number::Kind::glyph);
  CHECK(glyph.value == 12U);
  CHECK(require_value(GlyphList::number("glyph300")).value == 300U);
  const GlyphList::Number cid = require_value(GlyphList::number("cid1234"));
  CHECK(cid.kind == GlyphList::Number::Kind::cid);
  CHECK(cid.value == 1234U);
  // Cdd is decimal, Cxx hexadecimal when it is not decimal.
  const GlyphList::Number decimal = require_value(GlyphList::number("C65"));
  CHECK(decimal.kind == GlyphList::Number::Kind::code);
  CHECK(decimal.value == 65U);
  CHECK(require_value(GlyphList::number("C4A")).value == 0x4AU);
  CHECK_FALSE(GlyphList::number("cent").has_value());
  CHECK_FALSE(GlyphList::number("A").has_value());
}

TEST_CASE("a name nothing answers is unreadable") {
  CHECK_FALSE(GlyphList::to_unicode("nothing_known_here").has_value());
  CHECK_FALSE(GlyphList::to_unicode("xyzzy").has_value());
  CHECK_FALSE(GlyphList::to_unicode("").has_value());
}

TEST_CASE("control characters, private-use code points and U+FFFD are never text") {
  CHECK(GlyphList::usable(U'\t'));
  CHECK_FALSE(GlyphList::usable(U'\n'));
  CHECK_FALSE(GlyphList::usable(U'\x01'));
  CHECK_FALSE(GlyphList::usable(U'\x85'));
  CHECK_FALSE(GlyphList::usable(U''));
  CHECK_FALSE(GlyphList::usable(U''));
  CHECK_FALSE(GlyphList::usable(U'\U000F0000'));
  CHECK_FALSE(GlyphList::usable(U'�'));
  CHECK_FALSE(GlyphList::usable(U'￿'));
  CHECK(GlyphList::usable(U'A'));
  CHECK(GlyphList::usable(U' '));
  // The AGL maps a few names into the private-use area; they give no text.
  CHECK_FALSE(GlyphList::to_unicode("uniE000").has_value());
}

TEST_CASE("the seven Latin ligatures decompose to their letters") {
  CHECK(Ligatures::decompose("\xEF\xAC\x80") == std::optional<std::string>("ff"));
  CHECK(Ligatures::decompose("\xEF\xAC\x81") == std::optional<std::string>("fi"));
  CHECK(Ligatures::decompose("\xEF\xAC\x82") == std::optional<std::string>("fl"));
  CHECK(Ligatures::decompose("\xEF\xAC\x83") == std::optional<std::string>("ffi"));
  CHECK(Ligatures::decompose("\xEF\xAC\x84") == std::optional<std::string>("ffl"));
  CHECK(Ligatures::decompose("\xEF\xAC\x85") == std::optional<std::string>("\xC5\xBFt"));
  CHECK(Ligatures::decompose("\xEF\xAC\x86") == std::optional<std::string>("st"));
  CHECK(Ligatures::decompose("o\xEF\xAC\x83"
                             "ce") == std::optional<std::string>("office"));
  CHECK_FALSE(Ligatures::decompose("office").has_value());
  // A combining sequence is kept as written.
  CHECK_FALSE(Ligatures::decompose("e\xCC\x81").has_value());
}

}  // namespace
