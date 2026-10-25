// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::DctDecoder over JPEGs encoded in the test by
// libjpeg-turbo's own compressor from known pixels: grey, RGB, CMYK written as
// YCCK with its Adobe marker, each decoded within a stated tolerance; a
// truncated JPEG decoded as far as it goes with libjpeg's warning, a JPEG cut
// inside its header refused as a fault (with nothing left allocated, which
// the sanitizer run checks), and a JPEG larger than the budget's per-stream
// limit refused with limit_exceeded.

#include "images/dct_decoder.hpp"

#include "images/image_fixture.hpp"
#include "platform/budget.hpp"
#include "platform/error.hpp"

#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace {

using pdfkit::images::DctDecoder;
using pdfkit::platform::Budget;

std::vector<std::uint8_t> encode(int width, int height, int components,
                                 const std::vector<std::uint8_t>& pixels, bool ycck = false) {
  const std::string jpeg = pdfkit::test::encode_jpeg(width, height, components, pixels, ycck);
  return {jpeg.begin(), jpeg.end()};
}

DctDecoder::Result decode(const std::vector<std::uint8_t>& jpeg, int transform = -1) {
  Budget budget;
  return DctDecoder::decode(std::as_bytes(std::span(jpeg)), transform, budget, "object 7 0");
}

int largest_difference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  REQUIRE(a.size() == b.size());
  int largest = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    largest = std::max(largest, std::abs(int{a[i]} - int{b[i]}));
  }
  return largest;
}

// A smooth image: each sample a gradient over x and y, offset per component.
std::vector<std::uint8_t> gradient(int width, int height, int components) {
  std::vector<std::uint8_t> pixels;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < components; ++c) {
        pixels.push_back(static_cast<std::uint8_t>((x * 8 + y * 4 + c * 40) % 256));
      }
    }
  }
  return pixels;
}

TEST_CASE("grey and RGB JPEGs decode within a small tolerance") {
  const std::vector<std::uint8_t> grey = gradient(16, 16, 1);
  const DctDecoder::Result one = decode(encode(16, 16, 1, grey));
  REQUIRE(one.fault.empty());
  CHECK(one.components == 1U);
  CHECK(one.width == 16U);
  CHECK(one.height == 16U);
  CHECK(largest_difference(one.samples, grey) <= 8);

  const std::vector<std::uint8_t> rgb = gradient(16, 8, 3);
  const DctDecoder::Result three = decode(encode(16, 8, 3, rgb));
  REQUIRE(three.fault.empty());
  CHECK(three.components == 3U);
  CHECK_FALSE(three.adobe);
  CHECK(largest_difference(three.samples, rgb) <= 12);
}

TEST_CASE("a CMYK JPEG stored as YCCK with an Adobe marker decodes to its CMYK") {
  const std::vector<std::uint8_t> cmyk = gradient(8, 8, 4);
  const DctDecoder::Result four = decode(encode(8, 8, 4, cmyk, true));
  REQUIRE(four.fault.empty());
  CHECK(four.components == 4U);
  // libjpeg writes an Adobe marker for a YCCK JPEG.
  CHECK(four.adobe);
  CHECK(largest_difference(four.samples, cmyk) <= 12);
  // Stored as plain CMYK, the samples come back the same way.
  const DctDecoder::Result plain = decode(encode(8, 8, 4, cmyk), 0);
  REQUIRE(plain.fault.empty());
  CHECK(largest_difference(plain.samples, cmyk) <= 12);
}

TEST_CASE("a truncated JPEG decodes as far as it goes; a cut header is a fault") {
  const std::vector<std::uint8_t> jpeg = encode(64, 64, 1, gradient(64, 64, 1));
  std::vector<std::uint8_t> half(jpeg.begin(),
                                 jpeg.begin() + static_cast<std::ptrdiff_t>(jpeg.size() / 2));
  const DctDecoder::Result partial = decode(half);
  CHECK(partial.fault.empty());
  CHECK(partial.warnings > 0U);
  CHECK(partial.samples.size() == 64U * 64U);

  const std::vector<std::uint8_t> cut(jpeg.begin(), jpeg.begin() + 20);
  const DctDecoder::Result failed = decode(cut);
  CHECK_FALSE(failed.fault.empty());
  CHECK(failed.samples.empty());

  const std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0, 0, 0, 0};
  CHECK(decode(png).fault.find("Not a JPEG") != std::string::npos);
}

TEST_CASE("a JPEG past the per-stream limit is refused before it decodes") {
  const std::vector<std::uint8_t> jpeg = encode(32, 32, 3, gradient(32, 32, 3));
  Budget::Limits limits;
  limits.stream_bytes = 1000;
  Budget budget(limits);
  try {
    (void)DctDecoder::decode(std::as_bytes(std::span(jpeg)), -1, budget, "object 7 0");
    FAIL("the decode was not refused");
  } catch (const pdfkit::platform::Error& error) {
    CHECK(error.code() == pdfkit::platform::ErrorCode::limit_exceeded);
  }
}

}  // namespace
