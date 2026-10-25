// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::ColourSpace over known samples: the device spaces and
// CMYK through the Adobe table, ICCBased read by /N, CalRGB as its device
// equivalent, Lab's white and black, Indexed with a short lookup (padded) and
// a lookup stream, Separation through a type 2 and a type 4 tint transform,
// DeviceN with four components through a type 4 transform, the inline
// abbreviations and a named resource, and Pattern refused.

#include "images/colour_space.hpp"

#include "images/adobe_cmyk.hpp"
#include "images/decode_fault.hpp"
#include "images/image_fixture.hpp"
#include "syntax/object.hpp"

#include <doctest.h>

#include <array>
#include <string>
#include <vector>

namespace {

using pdfkit::images::ColourSpace;
using pdfkit::images::DecodeFault;
using pdfkit::syntax::Object;
using pdfkit::test::bytes_of;
using pdfkit::test::ImageFixture;

std::vector<double> convert(const ColourSpace& space, std::vector<double> in) {
  std::vector<double> out(space.output_channels());
  space.convert(in, out);
  return out;
}

std::shared_ptr<const ColourSpace> parse_object(const std::string& body) {
  ImageFixture fixture;
  const std::uint32_t number = fixture.object(body);
  auto& document = fixture.open();
  return ColourSpace::parse(document, fixture.get(number));
}

TEST_CASE("the device spaces convert directly, CMYK through the Adobe table") {
  const auto gray = ColourSpace::device(1);
  CHECK(gray->output_channels() == 1U);
  CHECK(convert(*gray, {0.25})[0] == doctest::Approx(0.25));
  const auto rgb = ColourSpace::device(3);
  CHECK(convert(*rgb, {0.1, 0.2, 0.3}) == std::vector<double>{0.1, 0.2, 0.3});
  const auto cmyk = ColourSpace::device(4);
  CHECK(cmyk->is_cmyk());
  // No ink is white; full black is near the table's corner sample {34, 30, 31}.
  CHECK(convert(*cmyk, {0.0, 0.0, 0.0, 0.0}) == std::vector<double>{1.0, 1.0, 1.0});
  CHECK(convert(*cmyk, {0.0, 0.0, 0.0, 1.0}) ==
        std::vector<double>{35.0 / 255.0, 31.0 / 255.0, 32.0 / 255.0});
  const std::uint8_t none = 0;
  CHECK(pdfkit::images::AdobeCmyk::to_rgb(none, none, none, none) ==
        std::array<std::uint8_t, 3>{255, 255, 255});
  CHECK(cmyk->default_decode(8) == std::vector<double>{0, 1, 0, 1, 0, 1, 0, 1});
}

TEST_CASE("names, abbreviations and resources resolve to their spaces") {
  ImageFixture fixture;
  const std::uint32_t named = fixture.object("<< /CS0 [/CalRGB << /WhitePoint [1 1 1] >>] >>");
  auto& document = fixture.open();
  CHECK(ColourSpace::parse(document, Object::make_name("G"))->components() == 1U);
  CHECK(ColourSpace::parse(document, Object::make_name("RGB"))->components() == 3U);
  CHECK(ColourSpace::parse(document, Object::make_name("CMYK"))->components() == 4U);
  const auto resource = ColourSpace::parse(document, Object::make_name("CS0"), fixture.get(named));
  CHECK(resource->family() == ColourSpace::Family::device_rgb);
  CHECK_THROWS_AS((void)ColourSpace::parse(document, Object::make_name("Pattern")), DecodeFault);
  CHECK_THROWS_AS((void)ColourSpace::parse(document, Object::make_name("Unknown")), DecodeFault);
}

TEST_CASE("an ICCBased space's /N decides its device equivalent") {
  ImageFixture fixture;
  const std::uint32_t profile = fixture.stream("/N 3", "profile bytes");
  const std::uint32_t number = fixture.object("[/ICCBased " + std::to_string(profile) + " 0 R]");
  auto& document = fixture.open();
  const auto space = ColourSpace::parse(document, fixture.get(number));
  CHECK(space->components() == 3U);
  CHECK(space->output_channels() == 3U);
  CHECK(space->name() == "ICCBased");
}

TEST_CASE("Lab converts white and black and keeps neutral greys neutral") {
  const auto lab = parse_object("[/Lab << /WhitePoint [0.9505 1 1.089] >>]");
  CHECK(lab->default_decode(8) == std::vector<double>{0, 100, -100, 100, -100, 100});
  const auto white = convert(*lab, {100, 0, 0});
  CHECK(white[0] == doctest::Approx(1.0).epsilon(0.01));
  CHECK(white[2] == doctest::Approx(1.0).epsilon(0.01));
  const auto black = convert(*lab, {0, 0, 0});
  CHECK(black[1] == doctest::Approx(0.0).epsilon(0.01));
  const auto grey = convert(*lab, {50, 0, 0});
  CHECK(grey[0] == doctest::Approx(grey[1]).epsilon(0.01));
  CHECK(grey[1] == doctest::Approx(grey[2]).epsilon(0.01));
}

TEST_CASE("Indexed looks its index up, clamped to hival, a short lookup padded") {
  const auto space = parse_object("[/Indexed /DeviceRGB 2 <FF0000 00FF00>]");
  CHECK(space->family() == ColourSpace::Family::indexed);
  CHECK(space->output_channels() == 3U);
  CHECK(space->default_decode(4) == std::vector<double>{0, 15});
  CHECK(convert(*space, {0})[0] == doctest::Approx(1.0));
  CHECK(convert(*space, {1})[1] == doctest::Approx(1.0));
  // Index 2 lies past the six bytes given: zeros.
  CHECK(convert(*space, {2}) == std::vector<double>{0.0, 0.0, 0.0});
  // An index above hival is hival.
  CHECK(convert(*space, {200}) == std::vector<double>{0.0, 0.0, 0.0});
}

TEST_CASE("Indexed reads its lookup from a stream") {
  ImageFixture fixture;
  const std::uint32_t lookup = fixture.stream("", bytes_of({0, 128, 255}));
  const std::uint32_t number =
      fixture.object("[/Indexed /DeviceGray 2 " + std::to_string(lookup) + " 0 R]");
  auto& document = fixture.open();
  const auto space = ColourSpace::parse(document, fixture.get(number));
  CHECK(convert(*space, {1})[0] == doctest::Approx(128.0 / 255.0));
  CHECK(convert(*space, {2})[0] == doctest::Approx(1.0));
}

TEST_CASE("Separation converts through its tint transform") {
  const auto exponential = parse_object(
      "[/Separation /Spot /DeviceCMYK << /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] "
      "/C1 [0 0 0 1] /N 1 >>]");
  CHECK(exponential->components() == 1U);
  CHECK(exponential->output_channels() == 3U);
  CHECK(convert(*exponential, {1.0})[0] == doctest::Approx(35.0 / 255.0));
  CHECK(convert(*exponential, {0.0})[0] == doctest::Approx(1.0));

  ImageFixture fixture;
  const std::uint32_t program =
      fixture.stream("/FunctionType 4 /Domain [0 1] /Range [0 1]", "{ 1 exch sub }");
  const std::uint32_t number =
      fixture.object("[/Separation /Ink /DeviceGray " + std::to_string(program) + " 0 R]");
  auto& document = fixture.open();
  const auto calculator = ColourSpace::parse(document, fixture.get(number));
  CHECK(convert(*calculator, {0.25})[0] == doctest::Approx(0.75));

  const auto none = parse_object(
      "[/Separation /None /DeviceGray << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>]");
  CHECK(convert(*none, {1.0})[0] == doctest::Approx(1.0));
}

TEST_CASE("DeviceN with four components converts through its transform") {
  ImageFixture fixture;
  // The four tints map to CMYK unchanged.
  const std::uint32_t program =
      fixture.stream("/FunctionType 4 /Domain [0 1 0 1 0 1 0 1] /Range [0 1 0 1 0 1 0 1]", "{ }");
  const std::uint32_t number =
      fixture.object("[/DeviceN [/C /M /Y /K] /DeviceCMYK " + std::to_string(program) + " 0 R]");
  auto& document = fixture.open();
  const auto space = ColourSpace::parse(document, fixture.get(number));
  CHECK(space->components() == 4U);
  // Full magenta: green is the darkest channel.
  const auto out = convert(*space, {0.0, 1.0, 0.0, 0.0});
  CHECK(out[1] < out[0]);
  CHECK(out[1] < out[2]);
}

}  // namespace
