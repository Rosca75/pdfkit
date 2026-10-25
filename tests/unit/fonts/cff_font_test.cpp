// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of fonts::CffFont over programs written by FontBuilder: the charset
// in each format, standard and custom strings, the standard and a custom
// encoding, widths from the charstring (nominal plus operand) and from
// defaultWidthX, a width read through a local subroutine, a CID-keyed font's
// charset and FDSelect, and damaged programs (a bad INDEX count, truncation)
// that never throw.

#include "fonts/cff_font.hpp"

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "support/font_builder.hpp"
#include "support/require_value.hpp"

#include <doctest.h>

#include <optional>
#include <string>

namespace {

using pdfkit::fonts::CffFont;
using pdfkit::platform::Budget;
using pdfkit::test::FontBuilder;
using pdfkit::test::require_value;

FontBuilder::Cff sample() {
  FontBuilder::Cff spec;
  spec.glyph_names = {".notdef", "A", "B", "f_i", "space"};
  spec.widths = {std::nullopt, 100, std::nullopt, -50, 0};
  spec.default_width = 333;
  spec.nominal_width = 500;
  return spec;
}

CffFont parse(const std::string& program) {
  Budget budget;
  return require_value(CffFont::parse(pdfkit::platform::as_bytes(program), budget));
}

TEST_CASE("the charset names each glyph, in every charset format") {
  for (const int format : {0, 1, 2}) {
    CAPTURE(format);
    FontBuilder::Cff spec = sample();
    spec.charset_format = format;
    const CffFont font = parse(FontBuilder::cff(spec));
    CHECK_FALSE(font.is_cid());
    CHECK(font.glyph_count() == 5U);
    CHECK(font.glyph_name(0) == ".notdef");
    CHECK(font.glyph_name(1) == "A");
    CHECK(font.glyph_name(3) == "f_i");
    CHECK(font.glyph_by_name("space") == std::optional<std::uint32_t>(4));
    CHECK_FALSE(font.glyph_by_name("Z").has_value());
  }
}

TEST_CASE("the standard encoding and a custom encoding give codes their glyphs") {
  const CffFont standard = parse(FontBuilder::cff(sample()));
  CHECK(standard.standard_encoding());
  CHECK(standard.glyph_for_code(0x41) == std::optional<std::uint32_t>(1));
  CHECK(standard.glyph_for_code(0x20) == std::optional<std::uint32_t>(4));
  CHECK_FALSE(standard.glyph_for_code(0x43).has_value());
  FontBuilder::Cff spec = sample();
  spec.encoding = {{0x61, 1}, {0x62, 2}, {0x63, 3}};
  const CffFont custom = parse(FontBuilder::cff(spec));
  CHECK_FALSE(custom.standard_encoding());
  CHECK(custom.glyph_for_code(0x61) == std::optional<std::uint32_t>(1));
  CHECK(custom.glyph_for_code(0x63) == std::optional<std::uint32_t>(3));
  CHECK_FALSE(custom.glyph_for_code(0x41).has_value());
}

TEST_CASE("widths: nominalWidthX plus the charstring's operand, else defaultWidthX") {
  const CffFont font = parse(FontBuilder::cff(sample()));
  CHECK(font.width(1).value_or(-1.0) == doctest::Approx(600.0));
  CHECK(font.width(2).value_or(-1.0) == doctest::Approx(333.0));
  CHECK(font.width(3).value_or(-1.0) == doctest::Approx(450.0));
  CHECK(font.width(4).value_or(-1.0) == doctest::Approx(500.0));
  CHECK_FALSE(font.width(5).has_value());
}

TEST_CASE("a width pushed inside a local subroutine is read through the call") {
  FontBuilder::Cff spec = sample();
  spec.width_in_subroutine = true;
  const CffFont font = parse(FontBuilder::cff(spec));
  CHECK(font.width(1).value_or(-1.0) == doctest::Approx(600.0));
}

TEST_CASE("a CID-keyed font maps CIDs to glyphs through its charset") {
  FontBuilder::Cff spec;
  spec.cids = {0, 34, 35, 1000};
  spec.widths = {std::nullopt, 0, 100, std::nullopt};
  spec.default_width = 1000;
  spec.nominal_width = 600;
  const CffFont font = parse(FontBuilder::cff(spec));
  CHECK(font.is_cid());
  CHECK(font.glyph_for_cid(1000) == std::optional<std::uint32_t>(3));
  CHECK(font.glyph_for_cid(34) == std::optional<std::uint32_t>(1));
  CHECK_FALSE(font.glyph_for_cid(36).has_value());
  CHECK(font.glyph_name(1).empty());
  CHECK(font.width(1).value_or(-1.0) == doctest::Approx(600.0));
  CHECK(font.width(2).value_or(-1.0) == doctest::Approx(700.0));
  CHECK(font.width(3).value_or(-1.0) == doctest::Approx(1000.0));
}

TEST_CASE("a damaged program never throws") {
  const std::string full = FontBuilder::cff(sample());
  // A bad INDEX count in the name INDEX: far more objects than the bytes hold.
  std::string bad = full;
  bad[4] = '\x7F';
  bad[5] = '\x7F';
  Budget budget;
  CHECK_NOTHROW((void)CffFont::parse(pdfkit::platform::as_bytes(bad), budget));
  for (std::size_t length = 0; length < full.size(); length += 3) {
    CAPTURE(length);
    Budget each;
    std::optional<CffFont> font;
    CHECK_NOTHROW(font = CffFont::parse(
                      pdfkit::platform::as_bytes(std::string_view(full).substr(0, length)), each));
    if (font) {
      CHECK_NOTHROW((void)font->width(1));
      CHECK_NOTHROW((void)font->glyph_name(1));
      CHECK_NOTHROW((void)font->glyph_for_code(0x41));
    }
  }
}

}  // namespace
