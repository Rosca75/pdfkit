// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PixelImage is a decoded image as every consumer of the images layer reads
// it: the PNG writer, the agreement test against the independent reader and
// the pixel treatments (box, blur, re-encoding). Samples are grey or RGB,
// interleaved, row after row with no padding, at 1, 8 or 16 bits: a 1-bit
// image holds one byte per sample, 0 or 255, so every consumer indexes it the
// same way; a 16-bit image holds two bytes per sample, most significant
// first, as PNG stores them. A CMYK source keeps its CMYK samples beside their
// RGB conversion, because a treatment re-encodes in the source's space. A
// stencil (an /ImageMask image) is grey with one value marked transparent;
// an image may carry an 8-bit alpha plane.

#ifndef PDFKIT_IMAGES_PIXEL_IMAGE_HPP
#define PDFKIT_IMAGES_PIXEL_IMAGE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pdfkit::images {

class PixelImage {
 public:
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // 1 (grey) or 3 (RGB).
  std::uint32_t channels = 1;
  // 1, 8 or 16.
  std::uint32_t bits = 8;
  // width * height * channels samples of bytes_per_sample() bytes each.
  std::vector<std::uint8_t> samples;
  // The source's CMYK samples, 8-bit, four per pixel, when the source was CMYK.
  std::vector<std::uint8_t> cmyk;
  // A grey value (0 or 255 for a 1-bit image) that is transparent.
  std::optional<std::uint16_t> transparent;
  // An alpha plane, 8-bit, width * height bytes, or empty.
  std::vector<std::uint8_t> alpha;

  [[nodiscard]] std::size_t bytes_per_sample() const noexcept { return bits == 16 ? 2 : 1; }
  [[nodiscard]] std::size_t row_bytes() const noexcept {
    return static_cast<std::size_t>(width) * channels * bytes_per_sample();
  }
  // The sample of channel at (x, y) scaled to 0..255.
  [[nodiscard]] std::uint8_t sample8(std::uint32_t x, std::uint32_t y,
                                     std::uint32_t channel) const noexcept;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_PIXEL_IMAGE_HPP
