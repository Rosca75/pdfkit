// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::treatments::BoxPainter and PixelBuffer: a box in
// DeviceGray, DeviceRGB, DeviceCMYK, Indexed and a /Decode [1 0] image fills
// every sample inside with the sample tuple that reads as the asked colour
// and leaves every sample outside as it was; a box partly outside the image
// is cut to it; a colour key never makes the fill transparent; a stencil is
// filled with the value that paints; rows pack and unpack at every depth.

#include "images/treatments/box_painter.hpp"

#include "images/colour_space.hpp"
#include "images/image_fixture.hpp"
#include "images/treatments/pixel_buffer.hpp"
#include "platform/budget.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::images::ColourSpace;
using pdfkit::images::treatments::BoxPainter;
using pdfkit::images::treatments::PixelBuffer;
using pdfkit::platform::Budget;

const Budget budget;

// A buffer whose every sample is a distinct pattern value.
PixelBuffer patterned(std::uint32_t width, std::uint32_t height, std::uint32_t components,
                      std::uint32_t bits) {
  PixelBuffer buffer(width, height, components, bits, budget, "a test image");
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      for (std::uint32_t c = 0; c < components; ++c) {
        buffer.set(x, y, c,
                   static_cast<std::uint16_t>(((x * 7) + (y * 13) + (c * 29) + 5) &
                                              buffer.max_value()));
      }
    }
  }
  return buffer;
}

// Checks every sample: fill inside rect, as in before outside.
void check_box(const PixelBuffer& before, const PixelBuffer& after, const PixelBuffer::Rect& rect,
               const std::vector<std::uint16_t>& fill) {
  for (std::uint32_t y = 0; y < after.height(); ++y) {
    for (std::uint32_t x = 0; x < after.width(); ++x) {
      const bool inside = x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
      for (std::uint32_t c = 0; c < after.components(); ++c) {
        if (inside) {
          REQUIRE(after.at(x, y, c) == fill[c]);
        } else {
          REQUIRE(after.at(x, y, c) == before.at(x, y, c));
        }
      }
    }
  }
}

BoxPainter::Meaning device(std::uint32_t components, std::uint32_t bits) {
  BoxPainter::Meaning meaning;
  meaning.space = ColourSpace::device(components);
  meaning.decode = meaning.space->default_decode(bits);
  meaning.bits = bits;
  meaning.components = components;
  return meaning;
}

constexpr BoxPainter::Colour black{0.0, 0.0, 0.0};

}  // namespace

TEST_CASE("rows pack and unpack at every depth") {
  for (const std::uint32_t bits : {1U, 2U, 4U, 8U, 16U}) {
    const PixelBuffer buffer = patterned(13, 5, 3, bits);
    const auto packed = buffer.pack();
    CHECK(packed.size() == buffer.stride() * 5);
    const PixelBuffer back =
        PixelBuffer::unpack(packed, buffer.stride(), 13, 5, 3, bits, budget, "a test image");
    CHECK(back == buffer);
  }
}

TEST_CASE("a buffer past the budget is refused before it is reserved") {
  const Budget small(Budget::Limits{1024, 1024 * 1024, 1000});
  CHECK_THROWS(PixelBuffer(100, 100, 3, 8, small, "a large image"));
}

TEST_CASE("a box in DeviceGray, DeviceRGB and DeviceCMYK is black inside and untouched outside") {
  for (const std::uint32_t components : {1U, 3U, 4U}) {
    const BoxPainter::Meaning meaning = device(components, 8);
    const std::vector<std::uint16_t> fill = BoxPainter::nearest(meaning, black);
    if (components == 1) {
      CHECK(fill == std::vector<std::uint16_t>{0});
    } else if (components == 3) {
      CHECK(fill == std::vector<std::uint16_t>{0, 0, 0});
    } else {
      // Through the Adobe table the darkest reading is a rich black.
      const auto colour = BoxPainter::colour_of(meaning, fill);
      CHECK(colour[0] < 0.15);
    }
    const PixelBuffer before = patterned(20, 16, components, 8);
    PixelBuffer after = before;
    const PixelBuffer::Rect rect{3, 2, 11, 9};
    BoxPainter::paint(after, rect, fill);
    check_box(before, after, rect, fill);
  }
}

TEST_CASE("a /Decode [1 0] grey image is filled with the value that reads black") {
  BoxPainter::Meaning meaning = device(1, 4);
  meaning.decode = {1.0, 0.0};
  const std::vector<std::uint16_t> fill = BoxPainter::nearest(meaning, black);
  CHECK(fill == std::vector<std::uint16_t>{15});
  const PixelBuffer before = patterned(9, 9, 1, 4);
  PixelBuffer after = before;
  BoxPainter::paint(after, {0, 0, 4, 4}, fill);
  check_box(before, after, {0, 0, 4, 4}, fill);
}

TEST_CASE("an Indexed image is filled with the palette entry nearest the colour") {
  pdfkit::test::ImageFixture fixture;
  // Four entries: red, grey, dark blue, white.
  const std::uint32_t number = fixture.object(
      "[/Indexed /DeviceRGB 3 <FF0000 808080 000020 FFFFFF>]");
  auto& document = fixture.open();
  BoxPainter::Meaning meaning;
  meaning.space = ColourSpace::parse(document, fixture.get(number));
  meaning.decode = meaning.space->default_decode(8);
  meaning.bits = 8;
  meaning.components = 1;
  CHECK(BoxPainter::nearest(meaning, black) == std::vector<std::uint16_t>{2});
  CHECK(BoxPainter::nearest(meaning, {1.0, 0.1, 0.0}) == std::vector<std::uint16_t>{0});
  // Past hival the lookup holds nothing, so only entries 0 to 3 are chosen.
  const std::vector<std::uint16_t> fill = BoxPainter::nearest(meaning, black);
  const PixelBuffer before = patterned(10, 10, 1, 8);
  PixelBuffer after = before;
  BoxPainter::paint(after, {5, 5, 10, 10}, fill);
  check_box(before, after, {5, 5, 10, 10}, fill);
}

TEST_CASE("a box partly outside the image is cut to it") {
  const BoxPainter::Meaning meaning = device(3, 8);
  const std::vector<std::uint16_t> fill = BoxPainter::nearest(meaning, black);
  const PixelBuffer before = patterned(8, 6, 3, 8);
  PixelBuffer after = before;
  BoxPainter::paint(after, {5, 4, 40, 30}, fill);
  check_box(before, after, {5, 4, 8, 6}, fill);
  PixelBuffer untouched = before;
  BoxPainter::paint(untouched, {20, 20, 40, 30}, fill);
  CHECK(untouched == before);
}

TEST_CASE("a colour key never makes the box's fill transparent") {
  BoxPainter::Meaning meaning = device(1, 8);
  // Values 0 to 3 are masked out.
  meaning.colour_key = {0, 3};
  const std::vector<std::uint16_t> fill = BoxPainter::nearest(meaning, black);
  CHECK(fill == std::vector<std::uint16_t>{4});
}

TEST_CASE("a stencil is filled with the value that paints, and contrast picks the other") {
  BoxPainter::Meaning meaning;
  meaning.decode = {0.0, 1.0};
  meaning.bits = 1;
  CHECK(BoxPainter::nearest(meaning, black) == std::vector<std::uint16_t>{0});
  meaning.decode = {1.0, 0.0};
  CHECK(BoxPainter::nearest(meaning, black) == std::vector<std::uint16_t>{1});
  const BoxPainter::Meaning rgb = device(3, 8);
  CHECK(BoxPainter::contrast(rgb, std::vector<std::uint16_t>{0, 0, 0}) ==
        std::vector<std::uint16_t>{255, 255, 255});
  CHECK(BoxPainter::contrast(rgb, std::vector<std::uint16_t>{250, 250, 250}) ==
        std::vector<std::uint16_t>{0, 0, 0});
}
