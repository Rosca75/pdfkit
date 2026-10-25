// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Packing and unpacking of sample rows: each row starts on a byte boundary,
// samples run most significant bit first within a byte, and a 16-bit sample is
// two bytes, most significant first (ISO 32000-1 section 8.9.5.2, recalled),
// the layout every Flate image and every codec output of the images layer
// shares.

#include "images/treatments/pixel_buffer.hpp"

#include "platform/checked.hpp"

#include <algorithm>
#include <string>

namespace pdfkit::images::treatments {

PixelBuffer::PixelBuffer(std::uint32_t width, std::uint32_t height, std::uint32_t components,
                         std::uint32_t bits, const platform::Budget& budget, std::string_view what)
    : width_(width), height_(height), components_(components), bits_(bits) {
  const auto pixels = platform::checked_mul<std::uint64_t>(width, height, "the pixels of an image");
  const auto samples = platform::checked_mul<std::uint64_t>(pixels, components,
                                                            "the samples of an image");
  budget.check_claim(platform::checked_mul<std::uint64_t>(samples, 2, "the samples of an image"),
                     "the samples of " + std::string(what) + " held for a treatment");
  samples_.assign(static_cast<std::size_t>(samples), 0);
}

std::size_t PixelBuffer::stride() const noexcept {
  return ((static_cast<std::size_t>(width_) * components_ * bits_) + 7) / 8;
}

PixelBuffer PixelBuffer::unpack(platform::ByteSpan packed, std::size_t stride, std::uint32_t width,
                                std::uint32_t height, std::uint32_t components, std::uint32_t bits,
                                const platform::Budget& budget, std::string_view what) {
  PixelBuffer buffer(width, height, components, bits, budget, what);
  const std::size_t per_row = static_cast<std::size_t>(width) * components;
  std::size_t out = 0;
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::size_t row = static_cast<std::size_t>(y) * stride;
    for (std::size_t i = 0; i < per_row; ++i, ++out) {
      std::uint32_t value = 0;
      if (bits == 16) {
        const std::size_t at = row + (2 * i);
        const std::uint32_t high = at < packed.size() ? std::to_integer<std::uint32_t>(packed[at])
                                                      : 0;
        const std::uint32_t low =
            at + 1 < packed.size() ? std::to_integer<std::uint32_t>(packed[at + 1]) : 0;
        value = (high << 8U) | low;
      } else {
        const std::size_t bit = i * bits;
        const std::size_t at = row + (bit / 8);
        if (at < packed.size()) {
          const std::uint32_t byte = std::to_integer<std::uint32_t>(packed[at]);
          const auto shift = static_cast<std::uint32_t>(8 - bits - (bit % 8));
          value = (byte >> shift) & ((1U << bits) - 1U);
        }
      }
      buffer.samples_[out] = static_cast<std::uint16_t>(value);
    }
  }
  return buffer;
}

std::vector<std::byte> PixelBuffer::pack() const {
  const std::size_t row_bytes = stride();
  std::vector<std::byte> out(row_bytes * height_, std::byte{0});
  const std::size_t per_row = static_cast<std::size_t>(width_) * components_;
  std::size_t in = 0;
  for (std::uint32_t y = 0; y < height_; ++y) {
    const std::size_t row = static_cast<std::size_t>(y) * row_bytes;
    for (std::size_t i = 0; i < per_row; ++i, ++in) {
      const std::uint32_t value = samples_[in];
      if (bits_ == 16) {
        out[row + (2 * i)] = static_cast<std::byte>(value >> 8U);
        out[row + (2 * i) + 1] = static_cast<std::byte>(value & 0xFFU);
      } else {
        const std::size_t bit = i * bits_;
        const auto shift = static_cast<std::uint32_t>(8 - bits_ - (bit % 8));
        out[row + (bit / 8)] |= static_cast<std::byte>((value & max_value()) << shift);
      }
    }
  }
  return out;
}

PixelBuffer::Rect PixelBuffer::clip(const Rect& rect) const noexcept {
  Rect out{std::min(rect.left, width_), std::min(rect.top, height_), std::min(rect.right, width_),
           std::min(rect.bottom, height_)};
  if (out.empty()) {
    return {};
  }
  return out;
}

void PixelBuffer::set_pixel(std::uint32_t x, std::uint32_t y,
                            std::span<const std::uint16_t> values) noexcept {
  const std::size_t at = index(x, y);
  for (std::uint32_t c = 0; c < components_ && c < values.size(); ++c) {
    samples_[at + c] = values[c];
  }
}

}  // namespace pdfkit::images::treatments
