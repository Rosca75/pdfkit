// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the masks through images::ImageDecoder and images::Masks: an
// /SMask of the image's size and of another size (kept at its own size), a
// /Matte undone, a colour-key /Mask, a stencil /Mask stream, an /ImageMask
// image (painted black, the rest transparent, /Decode [1 0] inverting it),
// an image without a mask; and the base images of the raw pipeline (1-bit
// grey, Indexed, 16-bit, Separation, a short image padded and marked
// damaged, a missing /ColorSpace and an unknown filter as faults).

#include "images/masks.hpp"

#include "images/image_decoder.hpp"
#include "images/image_fixture.hpp"
#include "images/pixel_image.hpp"
#include "support/require_value.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using pdfkit::images::ImageDecoder;
using pdfkit::images::Masks;
using pdfkit::images::PixelImage;
using pdfkit::test::bytes_of;
using pdfkit::test::ImageFixture;
using pdfkit::test::require_value;

constexpr const char* rgb_entries = "/Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8";

std::string rgb_pixels() {
  return bytes_of({255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
}

ImageDecoder::Result run(ImageFixture& fixture, std::uint32_t number,
                         ImageDecoder::Part part = ImageDecoder::Part::base) {
  return ImageDecoder::decode(fixture.document(), fixture.stream_of(number), part);
}

PixelImage decoded(ImageFixture& fixture, std::uint32_t number,
                   ImageDecoder::Part part = ImageDecoder::Part::base) {
  ImageDecoder::Result result = run(fixture, number, part);
  INFO(result.fault);
  return require_value(std::move(result.image));
}

std::string ref(std::uint32_t number) {
  return " " + std::to_string(number) + " 0 R";
}

TEST_CASE("an SMask of the image's size is its mask, the base untouched") {
  ImageFixture fixture;
  const std::uint32_t smask = fixture.stream(
      "/Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8",
      bytes_of({0, 85, 170, 255}));
  const std::uint32_t image =
      fixture.image(std::string(rgb_entries) + " /SMask" + ref(smask), rgb_pixels());
  fixture.open();
  CHECK(Masks::kind(fixture.document(), fixture.stream_of(image).dictionary(), false) ==
        Masks::Kind::soft);
  const PixelImage base = decoded(fixture, image);
  CHECK(base.channels == 3U);
  const std::string pixels = rgb_pixels();
  CHECK(base.samples == std::vector<std::uint8_t>(pixels.begin(), pixels.end()));
  const PixelImage mask = decoded(fixture, image, ImageDecoder::Part::mask);
  CHECK(mask.channels == 1U);
  CHECK(mask.bits == 8U);
  CHECK(mask.samples == std::vector<std::uint8_t>{0, 85, 170, 255});
}

TEST_CASE("an SMask of another size is kept at its own size") {
  ImageFixture fixture;
  const std::uint32_t smask = fixture.stream(
      "/Type /XObject /Subtype /Image /Width 4 /Height 1 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8",
      bytes_of({1, 2, 3, 4}));
  const std::uint32_t image =
      fixture.image(std::string(rgb_entries) + " /SMask" + ref(smask), rgb_pixels());
  fixture.open();
  const PixelImage mask = decoded(fixture, image, ImageDecoder::Part::mask);
  CHECK(mask.width == 4U);
  CHECK(mask.height == 1U);
}

TEST_CASE("a Matte is undone where the soft mask covers the image") {
  ImageFixture fixture;
  const std::uint32_t smask = fixture.stream(
      "/Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8 /Matte [1]",
      bytes_of({255, 128}));
  const std::uint32_t image = fixture.image(
      "/Width 2 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8 /SMask" + ref(smask),
      bytes_of({100, 191}));
  fixture.open();
  const PixelImage base = decoded(fixture, image);
  CHECK(base.samples[0] == 100);
  // c = m + (c' - m) / a = 255 + (191 - 255) / (128 / 255), about 128.
  CHECK(base.samples[1] == doctest::Approx(128).epsilon(0.02));
}

TEST_CASE("a colour-key Mask masks the pixels inside its ranges") {
  ImageFixture fixture;
  const std::uint32_t image =
      fixture.image(std::string(rgb_entries) + " /Mask [250 255 0 10 0 10]", rgb_pixels());
  fixture.open();
  CHECK(Masks::kind(fixture.document(), fixture.stream_of(image).dictionary(), false) ==
        Masks::Kind::colour_key);
  const PixelImage mask = decoded(fixture, image, ImageDecoder::Part::mask);
  CHECK(mask.bits == 1U);
  // Only the red pixel lies inside every range.
  CHECK(mask.samples == std::vector<std::uint8_t>{0, 255, 255, 255});
  const std::array<std::uint16_t, 3> red{255, 0, 0};
  const std::array<std::uint32_t, 6> ranges{250, 255, 0, 10, 0, 10};
  CHECK(Masks::keyed(red, ranges));
  const std::array<std::uint16_t, 3> green{0, 255, 0};
  CHECK_FALSE(Masks::keyed(green, ranges));
}

TEST_CASE("a stencil Mask stream is an alpha where its samples paint") {
  ImageFixture fixture;
  const std::uint32_t stencil = fixture.stream(
      "/Type /XObject /Subtype /Image /Width 2 /Height 2 /ImageMask true", bytes_of({0x40, 0x80}));
  const std::uint32_t image =
      fixture.image(std::string(rgb_entries) + " /Mask" + ref(stencil), rgb_pixels());
  fixture.open();
  const PixelImage mask = decoded(fixture, image, ImageDecoder::Part::mask);
  CHECK(mask.bits == 1U);
  // Row 1 holds 0 1, row 2 holds 1 0; a 0 paints, so it is opaque.
  CHECK(mask.samples == std::vector<std::uint8_t>{255, 0, 0, 255});
}

TEST_CASE("an ImageMask image is painted black on transparency") {
  ImageFixture fixture;
  const std::uint32_t plain = fixture.image("/Width 3 /Height 1 /ImageMask true", bytes_of({0x40}));
  const std::uint32_t inverted =
      fixture.image("/Width 3 /Height 1 /ImageMask true /Decode [1 0]", bytes_of({0x40}));
  fixture.open();
  const PixelImage image = decoded(fixture, plain);
  CHECK(image.bits == 1U);
  CHECK(image.transparent == std::optional<std::uint16_t>(255));
  CHECK(image.samples == std::vector<std::uint8_t>{0, 255, 0});
  CHECK(decoded(fixture, inverted).samples == std::vector<std::uint8_t>{255, 0, 255});
}

TEST_CASE("an image without a mask says so") {
  ImageFixture fixture;
  const std::uint32_t image = fixture.image(std::string(rgb_entries), rgb_pixels());
  fixture.open();
  const ImageDecoder::Result result = run(fixture, image, ImageDecoder::Part::mask);
  CHECK_FALSE(result.image.has_value());
  CHECK(result.no_mask);
}

TEST_CASE("the raw pipeline reads grey, Indexed, 16-bit and Separation images") {
  ImageFixture fixture;
  const std::uint32_t bitonal = fixture.image(
      "/Width 3 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 1", bytes_of({0xA0, 0x40}));
  const std::uint32_t indexed = fixture.image(
      "/Width 2 /Height 1 /ColorSpace [/Indexed /DeviceRGB 1 <FF0000 0000FF>] "
      "/BitsPerComponent 4",
      bytes_of({0x10}));
  const std::uint32_t deep = fixture.image(
      "/Width 1 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 16", bytes_of({0x80, 0x01}));
  const std::uint32_t separation = fixture.image(
      "/Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace [/Separation /Spot /DeviceGray "
      "<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>]",
      bytes_of({0, 255}));
  fixture.open();
  const PixelImage one = decoded(fixture, bitonal);
  CHECK(one.bits == 1U);
  CHECK(one.samples == std::vector<std::uint8_t>{255, 0, 255, 0, 255, 0});
  const PixelImage colours = decoded(fixture, indexed);
  CHECK(colours.channels == 3U);
  CHECK(colours.samples == std::vector<std::uint8_t>{0, 0, 255, 255, 0, 0});
  const PixelImage sixteen = decoded(fixture, deep);
  CHECK(sixteen.bits == 16U);
  CHECK(sixteen.samples == std::vector<std::uint8_t>{0x80, 0x01});
  const PixelImage spot = decoded(fixture, separation);
  CHECK(spot.samples == std::vector<std::uint8_t>{255, 0});
}

TEST_CASE("a short image is padded and marked damaged; a fault names its cause") {
  ImageFixture fixture;
  const std::uint32_t short_image = fixture.image(
      "/Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 8", bytes_of({9, 9, 9}));
  const std::uint32_t no_space =
      fixture.image("/Width 2 /Height 2 /BitsPerComponent 8", bytes_of({1, 2, 3, 4}));
  const std::uint32_t unknown = fixture.image(
      "/Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /Frobnicate",
      bytes_of({1, 2, 3, 4}));
  const std::uint32_t bad_bits = fixture.image(
      "/Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 3", bytes_of({1, 2}));
  fixture.open();
  const ImageDecoder::Result padded = run(fixture, short_image);
  CHECK(padded.damaged);
  CHECK(require_value(padded.image).samples == std::vector<std::uint8_t>{9, 9, 9, 0});
  CHECK(run(fixture, no_space).fault.find("/ColorSpace") != std::string::npos);
  CHECK(run(fixture, unknown).fault.find("Frobnicate") != std::string::npos);
  CHECK(run(fixture, bad_bits).fault.find("/BitsPerComponent") != std::string::npos);
}

}  // namespace
