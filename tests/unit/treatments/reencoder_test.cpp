// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::treatments::Reencoder with ImageDecoder::samples: a Flate
// image round-trips at 1, 2, 4, 8 and 16 bits per component with every
// sample equal; a DCT image re-encoded as JPEG decodes with the library's own
// decoder to the painted buffer within the JPEG tolerance outside the box and
// to exactly the fill inside it once the box's edge loses one 8-pixel block;
// a CMYK JPEG stays four components; a bitonal CCITT source becomes 1-bit
// Flate whose /Decode keeps its meaning; the dictionary loses the encoding's
// entries and states the samples.

#include "images/treatments/reencoder.hpp"

#include "images/ccitt_test_encoder.hpp"
#include "images/image_decoder.hpp"
#include "images/image_fixture.hpp"
#include "images/treatments/box_painter.hpp"
#include "images/treatments/pixel_buffer.hpp"
#include "syntax/object.hpp"
#include "writer/serialiser.hpp"
#include "writer/stream_copier.hpp"

#include <doctest.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using pdfkit::images::ImageDecoder;
using pdfkit::images::treatments::BoxPainter;
using pdfkit::images::treatments::PixelBuffer;
using pdfkit::images::treatments::Reencoder;
using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;

// The entries of a dictionary as PDF text, without the brackets.
std::string entries_of(const Dictionary& dictionary) {
  std::string text;
  pdfkit::writer::Serialiser::write(
      text, Object::make_dictionary(dictionary),
      [](Object::Reference reference) -> std::optional<std::uint32_t> { return reference.number; });
  return text.substr(2, text.size() - 4);
}

std::string as_string(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The samples of a re-encoded image read back through the decoder.
ImageDecoder::Samples read_back(const Reencoder::Result& result) {
  pdfkit::test::ImageFixture fixture;
  Dictionary dictionary = result.dictionary;
  std::string data;
  if (result.jpeg) {
    data = as_string(result.data);
  } else {
    dictionary.set("Filter", Object::make_name("FlateDecode"));
    data = as_string(pdfkit::writer::StreamCopier::deflate(result.data));
  }
  const std::uint32_t number = fixture.image(entries_of(dictionary), data);
  auto& document = fixture.open();
  ImageDecoder::SamplesResult read =
      ImageDecoder::samples(document, fixture.stream_of(number), false);
  REQUIRE(read.samples);
  return std::move(*read.samples);
}

ImageDecoder::Samples samples_of(const std::string& entries, const std::string& data) {
  pdfkit::test::ImageFixture fixture;
  const std::uint32_t number = fixture.image(entries, data);
  auto& document = fixture.open();
  ImageDecoder::SamplesResult read =
      ImageDecoder::samples(document, fixture.stream_of(number), false);
  REQUIRE(read.samples);
  return std::move(*read.samples);
}

}  // namespace

TEST_CASE("a Flate image round-trips at every depth with every sample equal") {
  for (const int bits : {1, 2, 4, 8, 16}) {
    const int components = bits == 16 ? 3 : 1;
    const std::uint32_t width = 11;
    const std::uint32_t height = 7;
    const std::size_t row = ((width * static_cast<std::size_t>(components * bits)) + 7) / 8;
    std::string data(row * height, '\0');
    for (std::size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<char>((i * 37 + 11) & 0xFF);
    }
    const std::string space = components == 3 ? "/DeviceRGB" : "/DeviceGray";
    const ImageDecoder::Samples source =
        samples_of("/Width 11 /Height 7 /ColorSpace " + space + " /BitsPerComponent " +
                       std::to_string(bits) + " /Decode " +
                       (components == 3 ? "[0 1 0 1 0 1]" : "[1 0]"),
                   data);
    CHECK(source.codec.empty());
    PixelBuffer painted = source.buffer;
    BoxPainter::paint(painted, {2, 1, 6, 5}, std::vector<std::uint16_t>(static_cast<std::size_t>(components), 0));
    Dictionary original;
    original.set("Width", Object::make_integer(11));
    original.set("Height", Object::make_integer(7));
    original.set("ColorSpace", Object::make_name(space.substr(1)));
    original.set("BitsPerComponent", Object::make_integer(bits));
    original.set("Decode", Object::make_array({Object::make_integer(1), Object::make_integer(0)}));
    const Reencoder::Result result = Reencoder::encode(original, painted, "", false);
    CHECK(!result.jpeg);
    CHECK(result.dictionary.contains("Decode"));
    const ImageDecoder::Samples back = read_back(result);
    CHECK(back.buffer == painted);
  }
}

TEST_CASE("a DCT image re-encodes as JPEG: the box exact inside, the rest within tolerance") {
  const int width = 64;
  const int height = 48;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 3));
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::uint8_t>(64 + ((i / 3) % 64) + ((i % 3) * 20));
  }
  const ImageDecoder::Samples source =
      samples_of("/Width 64 /Height 48 /ColorSpace /DeviceRGB /BitsPerComponent 8 "
                 "/Filter /DCTDecode",
                 pdfkit::test::encode_jpeg(width, height, 3, pixels));
  CHECK(source.codec == "DCTDecode");
  PixelBuffer painted = source.buffer;
  const PixelBuffer::Rect box{8, 8, 40, 32};
  BoxPainter::paint(painted, box, std::vector<std::uint16_t>{0, 0, 0});
  Dictionary original;
  original.set("Width", Object::make_integer(width));
  original.set("Height", Object::make_integer(height));
  original.set("ColorSpace", Object::make_name("DeviceRGB"));
  original.set("BitsPerComponent", Object::make_integer(8));
  original.set("Filter", Object::make_name("DCTDecode"));
  original.set("DecodeParms", Object::make_dictionary(Dictionary()));
  const Reencoder::Result result = Reencoder::encode(original, painted, "DCTDecode", false);
  CHECK(result.jpeg);
  CHECK(*result.dictionary.get("Filter").name() == "DCTDecode");
  CHECK(!result.dictionary.contains("DecodeParms"));
  const ImageDecoder::Samples back = read_back(result);
  REQUIRE(back.buffer.width() == painted.width());
  for (std::uint32_t y = 0; y < painted.height(); ++y) {
    for (std::uint32_t x = 0; x < painted.width(); ++x) {
      const bool deep = x >= box.left + 8 && x < box.right - 8 && y >= box.top + 8 &&
                        y < box.bottom - 8;
      const bool outside = x < box.left || x >= box.right || y < box.top || y >= box.bottom;
      for (std::uint32_t c = 0; c < 3; ++c) {
        const int got = back.buffer.at(x, y, c);
        if (deep) {
          REQUIRE(got == 0);
        } else if (outside && (x + 8 < box.left || x >= box.right + 8 || y + 8 < box.top ||
                               y >= box.bottom + 8)) {
          REQUIRE(std::abs(got - painted.at(x, y, c)) <= 12);
        }
      }
    }
  }
}

TEST_CASE("a CMYK JPEG stays four components and keeps its samples") {
  const int width = 16;
  const int height = 16;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4), 40);
  const ImageDecoder::Samples source = samples_of(
      "/Width 16 /Height 16 /ColorSpace /DeviceCMYK /BitsPerComponent 8 /Filter /DCTDecode",
      pdfkit::test::encode_jpeg(width, height, 4, pixels));
  Dictionary original;
  original.set("ColorSpace", Object::make_name("DeviceCMYK"));
  const Reencoder::Result result = Reencoder::encode(original, source.buffer, "DCTDecode", false);
  const ImageDecoder::Samples back = read_back(result);
  CHECK(back.buffer.components() == 4U);
  CHECK(std::abs(back.buffer.at(5, 5, 2) - source.buffer.at(5, 5, 2)) <= 2);
}

TEST_CASE("a bitonal CCITT source becomes 1-bit Flate whose /Decode keeps its meaning") {
  // Eight rows of 16 pixels, black (0 bits) on the left half.
  std::vector<std::vector<bool>> rows(8, std::vector<bool>(16, false));
  for (auto& line : rows) {
    for (std::size_t x = 0; x < 8; ++x) {
      line[x] = true;
    }
  }
  const std::string encoded =
      as_string(pdfkit::test::CcittTestEncoder::encode(rows, pdfkit::test::CcittTestEncoder::Options{}));
  const ImageDecoder::Samples source = samples_of(
      "/Width 16 /Height 8 /ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0] "
      "/Filter /CCITTFaxDecode /DecodeParms << /K -1 /Columns 16 /Rows 8 >>",
      encoded);
  CHECK(source.codec == "CCITTFaxDecode");
  Dictionary original;
  original.set("ColorSpace", Object::make_name("DeviceGray"));
  original.set("Decode", Object::make_array({Object::make_integer(1), Object::make_integer(0)}));
  original.set("Filter", Object::make_name("CCITTFaxDecode"));
  const Reencoder::Result result =
      Reencoder::encode(original, source.buffer, "CCITTFaxDecode", false);
  CHECK(!result.jpeg);
  CHECK(result.dictionary.get("BitsPerComponent").integer() == 1);
  CHECK(result.dictionary.contains("Decode"));
  const ImageDecoder::Samples back = read_back(result);
  CHECK(back.buffer == source.buffer);
  CHECK(back.decode == source.decode);
}
