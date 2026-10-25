// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::treatments::CaptionDrawer: the caption takes the largest
// height that fits the box and is centred in it, drawn only inside it; a box
// too small for any height draws nothing; a character outside the placeholder
// alphabet is refused with the three-part message; a turned orientation lays
// the line along the rows; the compiled bitmap data hashes to what the
// generator recorded.

#include "images/treatments/caption_drawer.hpp"

#include "images/treatments/pixel_buffer.hpp"
#include "platform/budget.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using pdfkit::images::treatments::CaptionDrawer;
using pdfkit::images::treatments::PixelBuffer;

const pdfkit::platform::Budget budget;
const std::array<std::uint16_t, 1> ink{1};

// The bound of the inked pixels.
PixelBuffer::Rect inked(const PixelBuffer& buffer) {
  PixelBuffer::Rect out{std::numeric_limits<std::uint32_t>::max(),
                        std::numeric_limits<std::uint32_t>::max(), 0, 0};
  for (std::uint32_t y = 0; y < buffer.height(); ++y) {
    for (std::uint32_t x = 0; x < buffer.width(); ++x) {
      if (buffer.at(x, y, 0) == 1) {
        out.left = std::min(out.left, x);
        out.top = std::min(out.top, y);
        out.right = std::max(out.right, x + 1);
        out.bottom = std::max(out.bottom, y + 1);
      }
    }
  }
  return out;
}

}  // namespace

TEST_CASE("the caption takes the largest height that fits and is centred in the box") {
  PixelBuffer buffer(400, 200, 1, 8, budget, "a test image");
  const PixelBuffer::Rect box{50, 40, 350, 160};
  const std::uint32_t height =
      CaptionDrawer::draw(buffer, box, "REDACTED", ink, CaptionDrawer::Orientation{});
  // REDACTED at 48 pixels is narrower than 300, at 64 wider or taller than
  // the box's 120 rows? 64 rows fit; the width decides.
  const std::uint32_t chosen = CaptionDrawer::width("REDACTED", 64) <= 300 ? 64U : 48U;
  CHECK(height == chosen);
  const PixelBuffer::Rect ink_bound = inked(buffer);
  CHECK(ink_bound.left >= box.left);
  CHECK(ink_bound.right <= box.right);
  CHECK(ink_bound.top >= box.top);
  CHECK(ink_bound.bottom <= box.bottom);
  // The line's cell is centred: equal margins within a pixel of rounding
  // around the advance box.
  const std::uint32_t line = CaptionDrawer::width("REDACTED", height);
  const std::uint32_t left = box.left + ((box.width() - line + 1) / 2);
  CHECK(ink_bound.left >= left);
  CHECK(ink_bound.right <= left + line);
  const std::uint32_t top = box.top + ((box.height() - height + 1) / 2);
  CHECK(ink_bound.top >= top);
  CHECK(ink_bound.bottom <= top + height);
}

TEST_CASE("a box too small for any height draws nothing") {
  PixelBuffer buffer(40, 40, 1, 8, budget, "a test image");
  CHECK(CaptionDrawer::draw(buffer, {0, 0, 30, 6}, "REDACTED", ink, CaptionDrawer::Orientation{}) ==
        0U);
  CHECK(buffer == PixelBuffer(40, 40, 1, 8, budget, "a test image"));
}

TEST_CASE("a character outside the alphabet is refused with the three-part message") {
  PixelBuffer buffer(100, 40, 1, 8, budget, "a test image");
  CHECK(CaptionDrawer::outside("Redacted!") == "edact!");
  try {
    (void)CaptionDrawer::draw(buffer, buffer.bounds(), "Secret", ink, CaptionDrawer::Orientation{});
    FAIL("the caption was drawn");
  } catch (const pdfkit::platform::Error& error) {
    CHECK(error.code() == pdfkit::platform::ErrorCode::invalid_argument);
    CHECK(error.failed().find("ecret") != std::string::npos);
    CHECK(!error.expected().empty());
    CHECK(!error.fix().empty());
  }
}

TEST_CASE("a turned orientation lays the line along the rows") {
  PixelBuffer buffer(100, 400, 1, 8, budget, "a test image");
  // One caption pixel right is one row up; one down is one column right.
  const CaptionDrawer::Orientation turned{0, -1, 1, 0};
  const std::uint32_t height = CaptionDrawer::draw(buffer, buffer.bounds(), "AB", ink, turned);
  REQUIRE(height > 0U);
  const PixelBuffer::Rect bound = inked(buffer);
  CHECK(bound.height() > bound.width());
  CHECK(bound.width() <= height);
}

TEST_CASE("the compiled bitmap font hashes to what its generator recorded") {
  std::string text;
  for (const std::string& line : CaptionDrawer::canonical_lines()) {
    text += line;
    text += '\n';
  }
  const auto digest = pdfkit::platform::crypto::Sha2::digest(
      pdfkit::platform::crypto::Sha2::Variant::sha256, std::as_bytes(std::span(text)));
  std::string hex;
  constexpr std::string_view digits = "0123456789abcdef";
  for (const std::byte value : digest) {
    const auto byte = std::to_integer<unsigned>(value);
    hex += digits[byte >> 4U];
    hex += digits[byte & 0x0FU];
  }
  CHECK(hex == CaptionDrawer::recorded_sha256());
  CHECK(CaptionDrawer::heights().size() == 7U);
}
