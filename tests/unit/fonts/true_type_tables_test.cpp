// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of fonts::TrueTypeTables over programs written by FontBuilder: each
// cmap format, the glyph-to-Unicode map, hmtx with fewer metrics than glyphs,
// head's units per em, post format 2 names, loca and glyf's empty glyphs, and
// truncated programs that answer what they hold without throwing.

#include "fonts/true_type_tables.hpp"

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "support/font_builder.hpp"
#include "support/require_value.hpp"

#include <doctest.h>

#include <optional>
#include <string>

namespace {

using pdfkit::fonts::TrueTypeTables;
using pdfkit::platform::Budget;
using pdfkit::test::FontBuilder;
using pdfkit::test::require_value;

FontBuilder::TrueType sample() {
  FontBuilder::TrueType spec;
  spec.units_per_em = 2048;
  spec.advances = {1000, 1100, 1200};
  spec.glyph_count = 5;
  spec.post_names = {".notdef", "A", "B", "uni00E9", "C"};
  spec.cmaps.push_back({3, 1, 4, {{0x41, 1}, {0x42, 2}, {0xE9, 3}}});
  spec.cmaps.push_back({1, 0, 0, {{0x41, 1}, {0x42, 2}}});
  spec.cmaps.push_back({3, 0, 6, {{0xF041, 1}, {0xF042, 2}}});
  spec.cmaps.push_back({3, 10, 12, {{0x1F600, 4}}});
  spec.empty_glyphs = {false, false, true, false, false};
  return spec;
}

TrueTypeTables parse(const std::string& program) {
  Budget budget;
  return require_value(TrueTypeTables::parse(pdfkit::platform::as_bytes(program), budget));
}

TEST_CASE("every cmap format maps its codes to glyphs") {
  const TrueTypeTables tables = parse(FontBuilder::true_type(sample()));
  CHECK(tables.lookup(3, 1, 0x41) == std::optional<std::uint32_t>(1));
  CHECK(tables.lookup(3, 1, 0xE9) == std::optional<std::uint32_t>(3));
  CHECK_FALSE(tables.lookup(3, 1, 0x43).has_value());
  CHECK(tables.lookup(1, 0, 0x42) == std::optional<std::uint32_t>(2));
  CHECK_FALSE(tables.lookup(1, 0, 0x43).has_value());
  CHECK(tables.lookup(3, 0, 0xF042) == std::optional<std::uint32_t>(2));
  CHECK_FALSE(tables.lookup(3, 0, 0x42).has_value());
  CHECK(tables.lookup(3, 10, 0x1F600) == std::optional<std::uint32_t>(4));
  CHECK(tables.has_cmap(3, 0));
  CHECK_FALSE(tables.has_cmap(0, 3));
}

TEST_CASE("a glyph's Unicode comes from the preferred Unicode cmap") {
  const TrueTypeTables tables = parse(FontBuilder::true_type(sample()));
  // Every Unicode subtable answers: (3,10) gives glyph 4, (3,1) the others.
  CHECK(tables.unicode_of(4) == std::optional<char32_t>(U'\U0001F600'));
  CHECK(tables.unicode_of(1) == std::optional<char32_t>(U'A'));
  FontBuilder::TrueType bmp = sample();
  bmp.cmaps.pop_back();
  const TrueTypeTables only_bmp = parse(FontBuilder::true_type(bmp));
  CHECK(only_bmp.unicode_of(1) == std::optional<char32_t>(U'A'));
  CHECK(only_bmp.unicode_of(3) == std::optional<char32_t>(U'é'));
  CHECK_FALSE(only_bmp.unicode_of(0).has_value());
}

TEST_CASE("hmtx with fewer metrics than glyphs repeats the last advance") {
  const TrueTypeTables tables = parse(FontBuilder::true_type(sample()));
  CHECK(tables.units_per_em() == 2048);
  CHECK(tables.glyph_count() == 5U);
  CHECK(tables.advance(0) == std::optional<std::uint16_t>(1000));
  CHECK(tables.advance(2) == std::optional<std::uint16_t>(1200));
  CHECK(tables.advance(4) == std::optional<std::uint16_t>(1200));
  CHECK(tables.ascender() == 800);
  CHECK(tables.descender() == -200);
}

TEST_CASE("post format 2 names and loca's empty glyphs") {
  const TrueTypeTables tables = parse(FontBuilder::true_type(sample()));
  CHECK(tables.glyph_name(1) == "A");
  CHECK(tables.glyph_name(3) == "uni00E9");
  CHECK(tables.glyph_name(0) == ".notdef");
  CHECK(tables.glyph_name(9).empty());
  CHECK(tables.glyph_by_name("C") == std::optional<std::uint32_t>(4));
  CHECK_FALSE(tables.glyph_by_name("Z").has_value());
  CHECK(tables.is_empty(2) == std::optional<bool>(true));
  CHECK(tables.is_empty(1) == std::optional<bool>(false));
  CHECK_FALSE(tables.is_empty(7).has_value());
}

TEST_CASE("bytes without a table directory are not a TrueType program") {
  Budget budget;
  CHECK_FALSE(TrueTypeTables::parse(pdfkit::platform::as_bytes("%!PS-AdobeFont"), budget));
  CHECK_FALSE(TrueTypeTables::parse({}, budget));
}

TEST_CASE("a truncated program answers what it holds and never throws") {
  const std::string full = FontBuilder::true_type(sample());
  for (std::size_t length = 0; length < full.size(); length += 7) {
    CAPTURE(length);
    Budget budget;
    std::optional<TrueTypeTables> tables;
    CHECK_NOTHROW(
        tables = TrueTypeTables::parse(
            pdfkit::platform::as_bytes(std::string_view(full).substr(0, length)), budget));
    if (tables) {
      CHECK_NOTHROW((void)tables->lookup(3, 1, 0x41));
      CHECK_NOTHROW((void)tables->unicode_of(1));
      CHECK_NOTHROW((void)tables->advance(3));
    }
  }
}

TEST_CASE("an OpenType program hands its CFF table on") {
  FontBuilder::TrueType spec = sample();
  spec.cff = "CFFDATA";
  const TrueTypeTables tables = parse(FontBuilder::true_type(spec));
  REQUIRE(tables.cff().size() == 7U);
  CHECK(static_cast<char>(tables.cff()[0]) == 'C');
}

}  // namespace
