// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::PngWriter: a 2x2 image of each type (grey 1, 8 and 16
// bits, RGB, grey and RGB with alpha, a stencil with tRNS), read back by a
// PNG reader written here (chunks and their CRC through zlib's crc32, IDAT
// inflated through zlib, the None and Paeth filters undone); the header bytes
// against the bytes the specification fixes; and the same image written twice
// giving the same bytes.

#include "images/png_writer.hpp"

#include "images/pixel_image.hpp"

#include <doctest.h>
#include <zlib.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

using pdfkit::images::PixelImage;
using pdfkit::images::PngWriter;

struct Png {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  int bits = 0;
  int colour_type = 0;
  std::vector<std::uint8_t> rows;  // unfiltered, packed as PNG packs them
  std::vector<std::uint8_t> trns;
  std::vector<std::string> chunks;
};

std::uint32_t be32(const std::uint8_t* p) {
  return (std::uint32_t{p[0]} << 24U) | (std::uint32_t{p[1]} << 16U) | (std::uint32_t{p[2]} << 8U) |
         p[3];
}

int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) {
    return a;
  }
  return pb <= pc ? b : c;
}

Png read(const std::vector<std::uint8_t>& file) {
  Png png;
  REQUIRE(file.size() > 8);
  const std::vector<std::uint8_t> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  REQUIRE(std::vector<std::uint8_t>(file.begin(), file.begin() + 8) == signature);
  std::vector<std::uint8_t> idat;
  std::size_t at = 8;
  while (at + 12 <= file.size()) {
    const std::uint32_t length = be32(&file[at]);
    const std::string type(file.begin() + static_cast<std::ptrdiff_t>(at + 4),
                           file.begin() + static_cast<std::ptrdiff_t>(at + 8));
    const std::uint8_t* data = &file[at + 8];
    const uLong crc = crc32(crc32(0L, Z_NULL, 0), &file[at + 4], length + 4);
    CHECK(be32(&file[at + 8 + length]) == static_cast<std::uint32_t>(crc));
    png.chunks.push_back(type);
    if (type == "IHDR") {
      png.width = be32(data);
      png.height = be32(data + 4);
      png.bits = data[8];
      png.colour_type = data[9];
    } else if (type == "IDAT") {
      idat.insert(idat.end(), data, data + length);
    } else if (type == "tRNS") {
      png.trns.assign(data, data + length);
    }
    at += 12 + length;
  }
  const auto channels = static_cast<std::size_t>(
      std::map<int, int>{{0, 1}, {2, 3}, {4, 2}, {6, 4}}.at(png.colour_type));
  const std::size_t pixel =
      std::max<std::size_t>(1, channels * static_cast<std::size_t>(png.bits) / 8);
  const std::size_t row =
      (static_cast<std::size_t>(png.width) * channels * static_cast<std::size_t>(png.bits) + 7) / 8;
  std::vector<std::uint8_t> filtered((row + 1) * png.height);
  z_stream stream{};
  REQUIRE(inflateInit(&stream) == Z_OK);
  stream.next_in = idat.data();
  stream.avail_in = static_cast<uInt>(idat.size());
  stream.next_out = filtered.data();
  stream.avail_out = static_cast<uInt>(filtered.size());
  const int status = inflate(&stream, Z_FINISH);
  const auto size = static_cast<std::size_t>(stream.total_out);
  inflateEnd(&stream);
  REQUIRE(status == Z_STREAM_END);
  REQUIRE(size == filtered.size());
  png.rows.assign(row * png.height, 0);
  for (std::uint32_t y = 0; y < png.height; ++y) {
    const std::uint8_t filter = filtered[y * (row + 1)];
    const std::uint8_t* in = &filtered[y * (row + 1) + 1];
    std::uint8_t* out = &png.rows[y * row];
    const std::uint8_t* above = y > 0 ? out - row : nullptr;
    REQUIRE((filter == 0 || filter == 4));
    for (std::size_t i = 0; i < row; ++i) {
      int value = in[i];
      if (filter == 4) {
        const int a = i >= pixel ? out[i - pixel] : 0;
        const int b = above != nullptr ? above[i] : 0;
        const int c = above != nullptr && i >= pixel ? above[i - pixel] : 0;
        value += paeth(a, b, c);
      }
      out[i] = static_cast<std::uint8_t>(value);
    }
  }
  return png;
}

PixelImage image(std::uint32_t channels, std::uint32_t bits, std::vector<std::uint8_t> samples) {
  PixelImage out;
  out.width = 2;
  out.height = 2;
  out.channels = channels;
  out.bits = bits;
  out.samples = std::move(samples);
  return out;
}

TEST_CASE("an 8-bit grey image round-trips with its header bytes fixed") {
  const PixelImage grey = image(1, 8, {0, 64, 128, 255});
  const std::vector<std::uint8_t> file = PngWriter::write(grey);
  // IHDR: length 13, the type, width 2, height 2, 8 bits, grey, then zeros.
  const std::vector<std::uint8_t> header{0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0,
                                         2, 0, 0, 0,  2,   8,   0,   0,   0, 0};
  CHECK(std::vector<std::uint8_t>(file.begin() + 8, file.begin() + 29) == header);
  const Png png = read(file);
  CHECK(png.chunks == std::vector<std::string>{"IHDR", "IDAT", "IEND"});
  CHECK(png.rows == grey.samples);
  CHECK(PngWriter::write(grey) == file);
}

TEST_CASE("RGB, 16-bit and 1-bit images keep their samples") {
  const PixelImage rgb = image(3, 8, {255, 0, 0, 0, 255, 0, 0, 0, 255, 10, 20, 30});
  Png png = read(PngWriter::write(rgb));
  CHECK(png.colour_type == 2);
  CHECK(png.bits == 8);
  CHECK(png.rows == rgb.samples);

  const PixelImage deep = image(1, 16, {0x12, 0x34, 0xFF, 0xFF, 0, 0, 0x80, 0x01});
  png = read(PngWriter::write(deep));
  CHECK(png.bits == 16);
  CHECK(png.rows == deep.samples);

  const PixelImage bitonal = image(1, 1, {255, 0, 0, 255});
  png = read(PngWriter::write(bitonal));
  CHECK(png.bits == 1);
  // Row 1: 1 0 -> 0x80; row 2: 0 1 -> 0x40.
  CHECK(png.rows == std::vector<std::uint8_t>{0x80, 0x40});
}

TEST_CASE("an alpha plane becomes a grey-alpha or RGBA image") {
  PixelImage grey = image(1, 8, {1, 2, 3, 4});
  grey.alpha = {255, 0, 128, 255};
  Png png = read(PngWriter::write(grey));
  CHECK(png.colour_type == 4);
  CHECK(png.rows == std::vector<std::uint8_t>{1, 255, 2, 0, 3, 128, 4, 255});

  PixelImage rgb = image(3, 8, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
  rgb.alpha = {9, 8, 7, 6};
  png = read(PngWriter::write(rgb));
  CHECK(png.colour_type == 6);
  CHECK(png.rows[3] == 9);
  CHECK(png.rows[15] == 6);

  // A 1-bit image with alpha is widened to 8 bits.
  PixelImage bitonal = image(1, 1, {255, 0, 0, 255});
  bitonal.alpha = {255, 255, 0, 0};
  png = read(PngWriter::write(bitonal));
  CHECK(png.bits == 8);
  CHECK(png.rows == std::vector<std::uint8_t>{255, 255, 0, 255, 0, 0, 255, 0});
}

TEST_CASE("a stencil's transparent grey is written as tRNS") {
  PixelImage stencil = image(1, 1, {0, 255, 255, 0});
  stencil.transparent = 255;
  const Png png = read(PngWriter::write(stencil));
  CHECK(png.chunks == std::vector<std::string>{"IHDR", "tRNS", "IDAT", "IEND"});
  CHECK(png.trns == std::vector<std::uint8_t>{0, 1});
}

}  // namespace
