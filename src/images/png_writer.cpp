// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The PNG writer: the 8-byte signature, IHDR, an optional tRNS, one IDAT and
// IEND, each chunk with its length and CRC-32 (zlib's crc32), the rows
// filtered and then deflated in one zlib stream (the PNG specification, W3C,
// recalled). An alpha plane is written beside the samples (colour type 4 or
// 6), a 1-bit image with alpha is widened to 8 bits first, and a 16-bit image
// widens its alpha to 16 bits.

#include "images/png_writer.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace pdfkit::images {

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24U));
  out.push_back(static_cast<std::uint8_t>(value >> 16U));
  out.push_back(static_cast<std::uint8_t>(value >> 8U));
  out.push_back(static_cast<std::uint8_t>(value));
}

void chunk(std::vector<std::uint8_t>& out, std::string_view type,
           const std::vector<std::uint8_t>& data) {
  put32(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t start = out.size();
  for (const char c : type) {
    out.push_back(static_cast<std::uint8_t>(c));
  }
  out.insert(out.end(), data.begin(), data.end());
  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32_z(crc, out.data() + start, out.size() - start);
  put32(out, static_cast<std::uint32_t>(crc));
}

std::uint8_t paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) {
    return static_cast<std::uint8_t>(a);
  }
  return static_cast<std::uint8_t>(pb <= pc ? b : c);
}

std::vector<std::uint8_t> deflate_all(const std::vector<std::uint8_t>& data) {
  z_stream stream{};
  if (deflateInit(&stream, 6) != Z_OK) {
    throw std::runtime_error("zlib's deflateInit failed");
  }
  // zlib counts in 32-bit units, so the rows go in and the stream comes out
  // in pieces of at most 1 MiB.
  constexpr std::size_t piece = std::size_t{1} << 20U;
  std::vector<std::uint8_t> out;
  std::vector<std::uint8_t> buffer(piece);
  std::size_t offset = 0;
  int status = Z_OK;
  while (status != Z_STREAM_END) {
    const std::size_t take = std::min(piece, data.size() - offset);
    stream.next_in = const_cast<Bytef*>(data.data() + offset);
    stream.avail_in = static_cast<uInt>(take);
    offset += take;
    const int flush = offset == data.size() ? Z_FINISH : Z_NO_FLUSH;
    do {
      stream.next_out = buffer.data();
      stream.avail_out = static_cast<uInt>(buffer.size());
      status = deflate(&stream, flush);
      if (status == Z_STREAM_ERROR) {
        deflateEnd(&stream);
        throw std::runtime_error("zlib's deflate failed");
      }
      out.insert(out.end(), buffer.begin(),
                 buffer.begin() + static_cast<std::ptrdiff_t>(buffer.size() - stream.avail_out));
    } while (stream.avail_out == 0);
  }
  deflateEnd(&stream);
  return out;
}

}  // namespace

std::vector<std::uint8_t> PngWriter::write(const PixelImage& image) {
  const bool has_alpha = !image.alpha.empty();
  std::uint32_t bits = image.bits;
  if (has_alpha && bits == 1) {
    bits = 8;
  }
  const std::uint32_t channels = image.channels + (has_alpha ? 1U : 0U);
  const std::size_t sample_bytes = bits == 16 ? 2 : 1;
  const std::size_t pixel_bytes = channels * sample_bytes;
  const std::size_t row_bytes =
      bits == 1 ? (static_cast<std::size_t>(image.width) + 7) / 8 : image.width * pixel_bytes;
  // The rows as PNG stores them, before filtering.
  std::vector<std::uint8_t> raw(row_bytes * image.height, 0);
  const std::size_t source_bytes = image.bytes_per_sample();
  for (std::uint32_t y = 0; y < image.height; ++y) {
    std::uint8_t* row = raw.data() + y * row_bytes;
    const std::size_t source_row = static_cast<std::size_t>(y) * image.width * image.channels;
    if (bits == 1) {
      for (std::uint32_t x = 0; x < image.width; ++x) {
        const std::size_t index = source_row + x;
        if (index < image.samples.size() && image.samples[index] != 0) {
          row[x / 8] = static_cast<std::uint8_t>(row[x / 8] | (0x80U >> (x % 8)));
        }
      }
      continue;
    }
    for (std::uint32_t x = 0; x < image.width; ++x) {
      std::uint8_t* pixel = row + x * pixel_bytes;
      for (std::uint32_t c = 0; c < image.channels; ++c) {
        const std::size_t index =
            (source_row + static_cast<std::size_t>(x) * image.channels + c) * source_bytes;
        for (std::size_t b = 0; b < sample_bytes; ++b) {
          pixel[c * sample_bytes + b] =
              index + b < image.samples.size() ? image.samples[index + b] : 0;
        }
      }
      if (has_alpha) {
        const std::size_t index = static_cast<std::size_t>(y) * image.width + x;
        const std::uint8_t value = index < image.alpha.size() ? image.alpha[index] : 255;
        pixel[image.channels * sample_bytes] = value;
        if (sample_bytes == 2) {
          pixel[image.channels * sample_bytes + 1] = value;
        }
      }
    }
  }
  // Filtering: None for 1-bit rows, Paeth (type 4) otherwise.
  std::vector<std::uint8_t> filtered;
  filtered.reserve((row_bytes + 1) * image.height);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    const std::uint8_t* row = raw.data() + y * row_bytes;
    const std::uint8_t* above = y > 0 ? row - row_bytes : nullptr;
    if (bits == 1) {
      filtered.push_back(0);
      filtered.insert(filtered.end(), row, row + row_bytes);
      continue;
    }
    filtered.push_back(4);
    for (std::size_t i = 0; i < row_bytes; ++i) {
      const int a = i >= pixel_bytes ? row[i - pixel_bytes] : 0;
      const int b = above != nullptr ? above[i] : 0;
      const int c = above != nullptr && i >= pixel_bytes ? above[i - pixel_bytes] : 0;
      filtered.push_back(static_cast<std::uint8_t>(row[i] - paeth(a, b, c)));
    }
  }
  std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<std::uint8_t> header;
  put32(header, image.width);
  put32(header, image.height);
  header.push_back(static_cast<std::uint8_t>(bits));
  std::uint8_t colour_type = image.channels == 3 ? 2 : 0;
  if (has_alpha) {
    colour_type = static_cast<std::uint8_t>(colour_type + 4);
  }
  header.push_back(colour_type);
  header.push_back(0);
  header.push_back(0);
  header.push_back(0);
  chunk(out, "IHDR", header);
  if (image.transparent && !has_alpha && image.channels == 1) {
    std::uint16_t value = *image.transparent;
    if (bits == 1) {
      value = value != 0 ? 1 : 0;
    }
    chunk(out, "tRNS", {static_cast<std::uint8_t>(value >> 8U), static_cast<std::uint8_t>(value)});
  }
  chunk(out, "IDAT", deflate_all(filtered));
  chunk(out, "IEND", {});
  return out;
}

}  // namespace pdfkit::images
