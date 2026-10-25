// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::treatments::RegionMapper: a page rectangle maps to the
// expected pixel rectangle under an identity, a scaled, a turned and a
// flipped image matrix, rounded outward and cut to the image; a pixel
// rectangle maps back to its page bound; a caller's pixel rectangle is
// rounded outward and cut; a singular matrix or a miss gives no rectangle.

#include "images/treatments/region_mapper.hpp"

#include "text/matrix.hpp"
#include "text/rect.hpp"

#include <doctest.h>

namespace {

using pdfkit::images::treatments::PixelBuffer;
using pdfkit::images::treatments::RegionMapper;
using pdfkit::text::Matrix;
using pdfkit::text::Rect;

}  // namespace

TEST_CASE("under the identity the unit square holds the whole grid, row 0 at the top") {
  const auto pixels =
      RegionMapper::to_pixels(Matrix::identity(), 10, 20, Rect::of(0.0, 0.5, 0.5, 1.0));
  REQUIRE(pixels);
  CHECK(*pixels == PixelBuffer::Rect{0, 0, 5, 10});
}

TEST_CASE("a scaled image maps a page rectangle to its pixels, rounded outward") {
  // A 200 by 100 image drawn 400 by 200 points at (100, 300).
  const Matrix ctm{400.0, 0.0, 0.0, 200.0, 100.0, 300.0};
  const auto pixels = RegionMapper::to_pixels(ctm, 200, 100, Rect::of(141.0, 401.0, 200.0, 459.0));
  REQUIRE(pixels);
  // x: (141-100)/2 = 20.5 -> 20, (200-100)/2 = 50; rows from the top:
  // (500-459)/2 = 20.5 -> 20, (500-401)/2 = 49.5 -> 50.
  CHECK(*pixels == PixelBuffer::Rect{20, 20, 50, 50});
  const Rect back = RegionMapper::to_page(ctm, 200, 100, {20, 20, 50, 50});
  CHECK(back.left == doctest::Approx(140.0));
  CHECK(back.right == doctest::Approx(200.0));
  CHECK(back.bottom == doctest::Approx(400.0));
  CHECK(back.top == doctest::Approx(460.0));
}

TEST_CASE("a page rectangle on exact pixel edges keeps its size") {
  const Matrix ctm{100.0, 0.0, 0.0, 100.0, 0.0, 0.0};
  const PixelBuffer::Rect rect{10, 20, 30, 40};
  const auto pixels = RegionMapper::to_pixels(ctm, 100, 100, RegionMapper::to_page(ctm, 100, 100, rect));
  REQUIRE(pixels);
  CHECK(*pixels == rect);
}

TEST_CASE("a turned image (90 degrees) maps columns to the page's vertical") {
  // The unit square turned a quarter anticlockwise: u runs up the page, v runs
  // left; the image is 100 points wide and 50 high on the page at (200, 0).
  const Matrix ctm{0.0, 100.0, -50.0, 0.0, 200.0, 0.0};
  // A 10 by 4 image: the page rectangle x 150..200, y 0..50 is u 0..0.5 and
  // v 0..1 (the whole height) -> columns 0..5, every row.
  const auto pixels = RegionMapper::to_pixels(ctm, 10, 4, Rect::of(150.0, 0.0, 200.0, 50.0));
  REQUIRE(pixels);
  CHECK(*pixels == PixelBuffer::Rect{0, 0, 5, 4});
}

TEST_CASE("a flipped image maps its top row to the page's bottom") {
  // Drawn upside down: [w 0 0 -h x y+h].
  const Matrix ctm{100.0, 0.0, 0.0, -100.0, 0.0, 100.0};
  const auto pixels = RegionMapper::to_pixels(ctm, 100, 100, Rect::of(0.0, 0.0, 100.0, 10.0));
  REQUIRE(pixels);
  CHECK(*pixels == PixelBuffer::Rect{0, 0, 100, 10});
}

TEST_CASE("a miss, a singular matrix and a caller rectangle outside give no rectangle") {
  const Matrix ctm{100.0, 0.0, 0.0, 100.0, 0.0, 0.0};
  CHECK(!RegionMapper::to_pixels(ctm, 10, 10, Rect::of(200.0, 200.0, 300.0, 300.0)));
  CHECK(!RegionMapper::to_pixels(Matrix{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}, 10, 10,
                                 Rect::of(0.0, 0.0, 1.0, 1.0)));
  CHECK(!RegionMapper::clip({20.0, 20.0, 30.0, 30.0}, 10, 10));
  const auto clipped = RegionMapper::clip({-5.0, 2.5, 7.2, 40.0}, 10, 10);
  REQUIRE(clipped);
  CHECK(*clipped == PixelBuffer::Rect{0, 2, 8, 10});
}
