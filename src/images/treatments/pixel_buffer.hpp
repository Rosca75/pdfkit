// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PixelBuffer holds an image's samples as its encoding stores them, before
// /Decode and the colour space: the source's component count and bit depth
// (1, 2, 4, 8 or 16), one value per component per pixel, rows from the top of
// the image down (ISO 32000-1 section 8.9.5.1, recalled: the first row of
// samples is the top of the image). A treatment paints, mosaics and captions
// these values, and the re-encoder writes them back in the source's space and
// depth, so every sample outside a treated rectangle keeps its exact value.
// Values are kept unpacked, one 16-bit value per sample, so a treatment
// indexes every depth alike; the size (width times height times components,
// two bytes each) is checked against the decoding budget before anything is
// reserved (CLAUDE.md section 9, bounded allocation).

#ifndef PDFKIT_IMAGES_TREATMENTS_PIXEL_BUFFER_HPP
#define PDFKIT_IMAGES_TREATMENTS_PIXEL_BUFFER_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace pdfkit::images::treatments {

class PixelBuffer {
 public:
  // A rectangle of pixels: columns [left, right) and rows [top, bottom),
  // rows counted from the top of the image.
  struct Rect {
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;

    [[nodiscard]] bool empty() const noexcept { return left >= right || top >= bottom; }
    [[nodiscard]] std::uint32_t width() const noexcept { return empty() ? 0 : right - left; }
    [[nodiscard]] std::uint32_t height() const noexcept { return empty() ? 0 : bottom - top; }
    friend bool operator==(const Rect&, const Rect&) = default;
    friend auto operator<=>(const Rect&, const Rect&) = default;
  };

  PixelBuffer() = default;

  // A buffer of zero samples. Throws platform::Error (limit_exceeded) when
  // the samples would pass the budget's claim limit; what names the image.
  PixelBuffer(std::uint32_t width, std::uint32_t height, std::uint32_t components,
              std::uint32_t bits, const platform::Budget& budget, std::string_view what);

  // The samples of packed rows of stride bytes each (a short buffer reads its
  // missing samples as zero, as viewers read a short image).
  [[nodiscard]] static PixelBuffer unpack(platform::ByteSpan packed, std::size_t stride,
                                          std::uint32_t width, std::uint32_t height,
                                          std::uint32_t components, std::uint32_t bits,
                                          const platform::Budget& budget, std::string_view what);

  // The samples packed into rows padded to a byte boundary, most significant
  // bit first, 16-bit values most significant byte first (section 8.9.5.2,
  // recalled).
  [[nodiscard]] std::vector<std::byte> pack() const;

  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint32_t components() const noexcept { return components_; }
  [[nodiscard]] std::uint32_t bits() const noexcept { return bits_; }
  // The largest sample value, 2^bits - 1.
  [[nodiscard]] std::uint16_t max_value() const noexcept {
    return static_cast<std::uint16_t>((1U << bits_) - 1U);
  }
  // The bytes of one packed row.
  [[nodiscard]] std::size_t stride() const noexcept;
  // The whole image as a rectangle.
  [[nodiscard]] Rect bounds() const noexcept { return {0, 0, width_, height_}; }
  // rect cut to the image.
  [[nodiscard]] Rect clip(const Rect& rect) const noexcept;

  [[nodiscard]] std::uint16_t at(std::uint32_t x, std::uint32_t y,
                                 std::uint32_t component) const noexcept {
    return samples_[index(x, y) + component];
  }
  void set(std::uint32_t x, std::uint32_t y, std::uint32_t component,
           std::uint16_t value) noexcept {
    samples_[index(x, y) + component] = value;
  }
  // The components of the pixel at (x, y).
  [[nodiscard]] std::span<const std::uint16_t> pixel(std::uint32_t x,
                                                     std::uint32_t y) const noexcept {
    return {samples_.data() + index(x, y), components_};
  }
  void set_pixel(std::uint32_t x, std::uint32_t y, std::span<const std::uint16_t> values) noexcept;

  friend bool operator==(const PixelBuffer&, const PixelBuffer&) = default;

 private:
  [[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y) const noexcept {
    return ((static_cast<std::size_t>(y) * width_) + x) * components_;
  }

  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::uint32_t components_ = 1;
  std::uint32_t bits_ = 8;
  std::vector<std::uint16_t> samples_;
};

}  // namespace pdfkit::images::treatments

#endif  // PDFKIT_IMAGES_TREATMENTS_PIXEL_BUFFER_HPP
