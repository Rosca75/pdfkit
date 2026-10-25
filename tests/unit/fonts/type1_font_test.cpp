// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of fonts::Type1Font over programs written by FontBuilder: the
// built-in encoding in its two clear-text forms, hsbw and sbw widths after
// eexec and charstring decryption, binary and hexadecimal eexec, PFB segment
// headers, and a program whose eexec section ends early, which answers what
// it read without throwing.

#include "fonts/type1_font.hpp"

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "support/font_builder.hpp"
#include "support/require_value.hpp"

#include <doctest.h>

#include <optional>
#include <string>

namespace {

using pdfkit::fonts::Type1Font;
using pdfkit::platform::Budget;
using pdfkit::test::FontBuilder;
using pdfkit::test::require_value;

FontBuilder::Type1 sample() {
  FontBuilder::Type1 spec;
  spec.encoding = {{0x41, "A"}, {0x42, "Bee"}, {0x80, "Euro"}};
  spec.glyphs = {{".notdef", 0}, {"A", 722}, {"Bee", 667}, {"Euro", 556}};
  return spec;
}

Type1Font parse(const std::string& program) {
  Budget budget;
  return require_value(Type1Font::parse(pdfkit::platform::as_bytes(program), budget));
}

TEST_CASE("the clear-text encoding: dup entries, or StandardEncoding") {
  const Type1Font custom = parse(FontBuilder::type1(sample()));
  CHECK_FALSE(custom.standard_encoding());
  CHECK(custom.encoding_name(0x41) == "A");
  CHECK(custom.encoding_name(0x42) == "Bee");
  CHECK(custom.encoding_name(0x80) == "Euro");
  CHECK(custom.encoding_name(0x43).empty());
  FontBuilder::Type1 spec = sample();
  spec.encoding.clear();
  const Type1Font standard = parse(FontBuilder::type1(spec));
  CHECK(standard.standard_encoding());
  CHECK(standard.encoding_name(0x41) == "A");
  CHECK(standard.encoding_name(0x27) == "quoteright");
}

TEST_CASE("widths come from hsbw or sbw after both decryptions") {
  for (const bool sbw : {false, true}) {
    CAPTURE(sbw);
    FontBuilder::Type1 spec = sample();
    spec.sbw = sbw;
    const Type1Font font = parse(FontBuilder::type1(spec));
    CHECK(font.glyph_count() == 4U);
    CHECK(font.has_glyph("Bee"));
    CHECK(font.width("A").value_or(-1.0) == doctest::Approx(722.0));
    CHECK(font.width("Bee").value_or(-1.0) == doctest::Approx(667.0));
    CHECK(font.width(".notdef").value_or(-1.0) == doctest::Approx(0.0));
    CHECK_FALSE(font.width("Z").has_value());
  }
}

TEST_CASE("hexadecimal eexec and PFB segments read the same program") {
  FontBuilder::Type1 hex = sample();
  hex.hex_eexec = true;
  CHECK(parse(FontBuilder::type1(hex)).width("Euro").value_or(-1.0) == doctest::Approx(556.0));
  FontBuilder::Type1 pfb = sample();
  pfb.pfb = true;
  const Type1Font font = parse(FontBuilder::type1(pfb));
  CHECK(font.width("A").value_or(-1.0) == doctest::Approx(722.0));
  CHECK(font.encoding_name(0x42) == "Bee");
}

TEST_CASE("eexec decryption inverts encryption with the format's constants") {
  const std::vector<std::uint8_t> plain = {'d', 'u', 'p', ' ', 0, 255, 17};
  CHECK(Type1Font::decrypt(Type1Font::encrypt(plain, 55665), 55665) == plain);
  CHECK(Type1Font::decrypt(Type1Font::encrypt(plain, 4330), 4330) == plain);
  CHECK(Type1Font::encrypt(plain, 55665) != plain);
}

TEST_CASE("a program whose eexec section ends early answers what it read") {
  const std::string full = FontBuilder::type1(sample());
  for (std::size_t cut = 0; cut < 400; cut += 9) {
    CAPTURE(cut);
    FontBuilder::Type1 spec = sample();
    spec.truncate_eexec = cut;
    Budget budget;
    std::optional<Type1Font> font;
    CHECK_NOTHROW(
        font = Type1Font::parse(pdfkit::platform::as_bytes(FontBuilder::type1(spec)), budget));
    // The clear-text encoding survives any cut of the encrypted part.
    CHECK(require_value(std::move(font)).encoding_name(0x41) == "A");
  }
  for (std::size_t length = 0; length < full.size(); length += 11) {
    Budget budget;
    CHECK_NOTHROW((void)Type1Font::parse(
        pdfkit::platform::as_bytes(std::string_view(full).substr(0, length)), budget));
  }
}

}  // namespace
