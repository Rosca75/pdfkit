// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::treatments::MosaicBlur: every block of the rectangle is
// constant and holds its mean; the samples outside are untouched; the block
// is at least 8 pixels and at least a sixteenth of the smaller side; an
// Indexed image takes the palette entry nearest each block's mean colour; a
// bitonal image under 128 pixels is refused and the message offers a box.

#include "images/treatments/mosaic_blur.hpp"

#include "images/colour_space.hpp"
#include "images/image_fixture.hpp"
#include "images/treatments/pixel_buffer.hpp"
#include "platform/budget.hpp"
#include "platform/error.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>

namespace {

using pdfkit::images::ColourSpace;
using pdfkit::images::treatments::BoxPainter;
using pdfkit::images::treatments::MosaicBlur;
using pdfkit::images::treatments::PixelBuffer;

const pdfkit::platform::Budget& budget() {
  static const pdfkit::platform::Budget limits;
  return limits;
}

PixelBuffer gradient(std::uint32_t width, std::uint32_t height, std::uint32_t components) {
  PixelBuffer buffer(width, height, components, 8, budget(), "a test image");
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      for (std::uint32_t c = 0; c < components; ++c) {
        buffer.set(x, y, c, static_cast<std::uint16_t>(((x * 3) + (y * 5) + (c * 11)) & 0xFFU));
      }
    }
  }
  return buffer;
}

}  // namespace

TEST_CASE("the block is at least 8 pixels and a sixteenth of the smaller side") {
  CHECK(MosaicBlur::block_size(50, 40, false) == 8U);
  CHECK(MosaicBlur::block_size(1000, 400, false) == 25U);
  CHECK(MosaicBlur::block_size(130, 2000, false) == 9U);
  // A JPEG's block is a whole number of 8-pixel JPEG blocks.
  CHECK(MosaicBlur::block_size(130, 2000, true) == 16U);
  CHECK(MosaicBlur::block_size(50, 40, true) == 8U);
}

TEST_CASE("every block is constant, holds its mean, and the outside is untouched") {
  const PixelBuffer before = gradient(64, 48, 3);
  PixelBuffer after = before;
  BoxPainter::Meaning meaning;
  meaning.space = ColourSpace::device(3);
  meaning.components = 3;
  meaning.decode = meaning.space->default_decode(8);
  const PixelBuffer::Rect rect{4, 6, 44, 37};
  const std::uint32_t block = 8;
  MosaicBlur::apply(after, rect, block, meaning);
  // The grid starts at the image's corner: rows 0, 8, 16, ..., cut by the
  // rectangle.
  for (std::uint32_t row = 0; row < rect.bottom; row += block) {
    for (std::uint32_t column = 0; column < rect.right; column += block) {
      const std::uint32_t top = std::max(row, rect.top);
      const std::uint32_t left = std::max(column, rect.left);
      const std::uint32_t bottom = std::min(rect.bottom, row + block);
      const std::uint32_t right = std::min(rect.right, column + block);
      if (top >= bottom || left >= right) {
        continue;
      }
      for (std::uint32_t c = 0; c < 3; ++c) {
        std::uint64_t sum = 0;
        for (std::uint32_t y = top; y < bottom; ++y) {
          for (std::uint32_t x = left; x < right; ++x) {
            sum += before.at(x, y, c);
            REQUIRE(after.at(x, y, c) == after.at(left, top, c));
          }
        }
        const std::uint64_t count = std::uint64_t{right - left} * (bottom - top);
        CHECK(after.at(left, top, c) == (2 * sum + count) / (2 * count));
      }
    }
  }
  for (std::uint32_t y = 0; y < 48; ++y) {
    for (std::uint32_t x = 0; x < 64; ++x) {
      if (x < rect.left || x >= rect.right || y < rect.top || y >= rect.bottom) {
        REQUIRE(after.pixel(x, y)[0] == before.pixel(x, y)[0]);
      }
    }
  }
}

TEST_CASE("an Indexed image takes the palette entry nearest each block's mean colour") {
  pdfkit::test::ImageFixture fixture;
  // Black, white and a middle grey.
  const std::uint32_t number = fixture.object("[/Indexed /DeviceGray 2 <00FF80>]");
  auto& document = fixture.open();
  BoxPainter::Meaning meaning;
  meaning.space = ColourSpace::parse(document, fixture.get(number));
  meaning.decode = meaning.space->default_decode(8);
  meaning.components = 1;
  PixelBuffer buffer(8, 8, 1, 8, budget(), "a test image");
  for (std::uint32_t y = 0; y < 8; ++y) {
    for (std::uint32_t x = 0; x < 8; ++x) {
      // A checkerboard of black and white: its mean is the middle grey.
      buffer.set(x, y, 0, static_cast<std::uint16_t>((x + y) % 2));
    }
  }
  MosaicBlur::apply(buffer, buffer.bounds(), 8, meaning);
  for (std::uint32_t y = 0; y < 8; ++y) {
    for (std::uint32_t x = 0; x < 8; ++x) {
      REQUIRE(buffer.at(x, y, 0) == 2);
    }
  }
}

TEST_CASE("a small bitonal image is refused and the message offers a box") {
  CHECK_NOTHROW(MosaicBlur::require_blurrable(true, 128, 300, "treatment 0"));
  CHECK_NOTHROW(MosaicBlur::require_blurrable(false, 20, 20, "treatment 0"));
  try {
    MosaicBlur::require_blurrable(true, 300, 100, "treatment 2");
    FAIL("a 300 by 100 bitonal image was accepted");
  } catch (const pdfkit::platform::Error& error) {
    CHECK(error.code() == pdfkit::platform::ErrorCode::invalid_argument);
    CHECK(error.failed().find("treatment 2") != std::string::npos);
    CHECK(error.fix().find("box") != std::string::npos);
  }
}
