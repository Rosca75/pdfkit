// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::treatments::SoftMaskTreatment: under a box the soft mask
// is opaque inside the rectangle and untouched outside, also when the mask
// is half the image's size; a stencil mask paints inside; under a blur the
// mask is mosaicked; a colour-key range does not punch a hole in the box.

#include "images/treatments/soft_mask_treatment.hpp"

#include "images/colour_space.hpp"
#include "images/masks.hpp"
#include "images/treatments/box_painter.hpp"
#include "images/treatments/pixel_buffer.hpp"
#include "platform/budget.hpp"

#include <doctest.h>

#include <cstdint>
#include <vector>

namespace {

using pdfkit::images::ColourSpace;
using pdfkit::images::treatments::BoxPainter;
using pdfkit::images::treatments::PixelBuffer;
using pdfkit::images::treatments::SoftMaskTreatment;

const pdfkit::platform::Budget budget;

BoxPainter::Meaning grey() {
  BoxPainter::Meaning meaning;
  meaning.space = ColourSpace::device(1);
  meaning.decode = {0.0, 1.0};
  meaning.components = 1;
  return meaning;
}

PixelBuffer faded(std::uint32_t width, std::uint32_t height) {
  PixelBuffer mask(width, height, 1, 8, budget, "a test mask");
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      mask.set(x, y, 0, static_cast<std::uint16_t>((x * 9 + y * 4) % 200));
    }
  }
  return mask;
}

}  // namespace

TEST_CASE("under a box the soft mask is opaque inside the rectangle only") {
  const PixelBuffer before = faded(40, 30);
  PixelBuffer mask = before;
  const PixelBuffer::Rect rect{5, 4, 20, 18};
  SoftMaskTreatment::opaque(mask, rect, 40, 30, grey());
  for (std::uint32_t y = 0; y < 30; ++y) {
    for (std::uint32_t x = 0; x < 40; ++x) {
      const bool inside = x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
      REQUIRE(mask.at(x, y, 0) == (inside ? 255 : before.at(x, y, 0)));
    }
  }
}

TEST_CASE("a mask of half the image's size is treated over the same part, rounded outward") {
  PixelBuffer mask = faded(20, 15);
  SoftMaskTreatment::opaque(mask, {5, 4, 20, 18}, 40, 30, grey());
  CHECK(SoftMaskTreatment::scale({5, 4, 20, 18}, 40, 30, mask) == PixelBuffer::Rect{2, 2, 10, 9});
  CHECK(mask.at(2, 2, 0) == 255);
  CHECK(mask.at(9, 8, 0) == 255);
  CHECK(mask.at(10, 9, 0) != 255);
}

TEST_CASE("a stencil mask paints inside the box") {
  PixelBuffer mask(16, 16, 1, 1, budget, "a test stencil");
  for (std::uint32_t y = 0; y < 16; ++y) {
    for (std::uint32_t x = 0; x < 16; ++x) {
      mask.set(x, y, 0, 1);
    }
  }
  BoxPainter::Meaning stencil;
  stencil.decode = {0.0, 1.0};
  stencil.bits = 1;
  SoftMaskTreatment::opaque(mask, {0, 0, 8, 8}, 16, 16, stencil);
  CHECK(mask.at(3, 3, 0) == 0);
  CHECK(mask.at(12, 12, 0) == 1);
}

TEST_CASE("under a blur the soft mask is mosaicked") {
  PixelBuffer mask = faded(32, 32);
  SoftMaskTreatment::blur(mask, {0, 0, 32, 32}, 32, 32, 8, grey());
  for (std::uint32_t y = 0; y < 32; ++y) {
    for (std::uint32_t x = 0; x < 32; ++x) {
      REQUIRE(mask.at(x, y, 0) == mask.at((x / 8) * 8, (y / 8) * 8, 0));
    }
  }
}

TEST_CASE("a colour-key range does not punch a hole in the box") {
  BoxPainter::Meaning meaning;
  meaning.space = ColourSpace::device(3);
  meaning.decode = meaning.space->default_decode(8);
  meaning.components = 3;
  // Pure black and its neighbours are masked out.
  meaning.colour_key = {0, 10, 0, 10, 0, 10};
  const std::vector<std::uint16_t> fill = BoxPainter::nearest(meaning, {0.0, 0.0, 0.0});
  CHECK(!pdfkit::images::Masks::keyed(fill, meaning.colour_key));
  const auto colour = BoxPainter::colour_of(meaning, fill);
  CHECK(colour[0] + colour[1] + colour[2] < 0.2);
}
