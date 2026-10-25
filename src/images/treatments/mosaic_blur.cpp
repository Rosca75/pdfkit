// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The mosaic. A block at the rectangle's right or bottom edge is cut short by
// the rectangle and averages what it holds. Means round half up, so a block of
// a bitonal image half black and half white becomes white; the result depends
// on the samples alone.

#include "images/treatments/mosaic_blur.hpp"

#include "images/decode_fault.hpp"
#include "platform/error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace pdfkit::images::treatments {

std::uint32_t MosaicBlur::block_size(std::uint32_t width, std::uint32_t height) noexcept {
  const std::uint32_t smaller = std::min(width, height);
  return std::max(minimum_block, (smaller + 15) / 16);
}

void MosaicBlur::require_blurrable(bool bitonal, std::uint32_t width, std::uint32_t height,
                                   std::string_view what) {
  if (bitonal && std::min(width, height) < minimum_bitonal_side) {
    throw platform::Error(
        platform::ErrorCode::invalid_argument,
        std::string(what) + " asks to blur a bitonal image of " + std::to_string(width) + " by " +
            std::to_string(height) + " pixels",
        "a bitonal image at least " + std::to_string(minimum_bitonal_side) +
            " pixels on its smaller side, where a mosaic block of at least " +
            std::to_string(minimum_block) + " pixels hides its shapes",
        "give this image the treatment \"box\" (which may carry a caption) or \"remove\"");
  }
}

void MosaicBlur::apply(PixelBuffer& buffer, const PixelBuffer::Rect& rect, std::uint32_t block,
                       const BoxPainter::Meaning& meaning) {
  const PixelBuffer::Rect area = buffer.clip(rect);
  if (area.empty() || block == 0) {
    return;
  }
  const std::uint32_t components = buffer.components();
  const bool indexed =
      meaning.space && meaning.space->family() == ColourSpace::Family::indexed && components == 1;
  // An Indexed image: every palette position's colour, read once.
  std::vector<BoxPainter::Colour> palette;
  if (indexed) {
    for (std::uint32_t value = 0; value <= buffer.max_value(); ++value) {
      const std::array<std::uint16_t, 1> tuple{static_cast<std::uint16_t>(value)};
      try {
        palette.push_back(BoxPainter::colour_of(meaning, tuple));
      } catch (const DecodeFault&) {
        palette.push_back({0.0, 0.0, 0.0});
      }
    }
  }
  std::vector<std::uint64_t> sums(components);
  std::vector<std::uint16_t> mean(components);
  for (std::uint32_t top = area.top; top < area.bottom; top += block) {
    const std::uint32_t bottom = std::min(area.bottom, top + block);
    for (std::uint32_t left = area.left; left < area.right; left += block) {
      const std::uint32_t right = std::min(area.right, left + block);
      const std::uint64_t count = std::uint64_t{right - left} * (bottom - top);
      if (indexed) {
        BoxPainter::Colour total{};
        for (std::uint32_t y = top; y < bottom; ++y) {
          for (std::uint32_t x = left; x < right; ++x) {
            const BoxPainter::Colour& colour = palette[buffer.at(x, y, 0)];
            for (std::size_t i = 0; i < 3; ++i) {
              total[i] += colour[i];
            }
          }
        }
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t value = 0; value < palette.size(); ++value) {
          double d = 0.0;
          for (std::size_t i = 0; i < 3; ++i) {
            const double delta = palette[value][i] - (total[i] / static_cast<double>(count));
            d += delta * delta;
          }
          if (d < best) {
            best = d;
            mean[0] = static_cast<std::uint16_t>(value);
          }
        }
      } else {
        std::ranges::fill(sums, 0);
        for (std::uint32_t y = top; y < bottom; ++y) {
          for (std::uint32_t x = left; x < right; ++x) {
            for (std::uint32_t c = 0; c < components; ++c) {
              sums[c] += buffer.at(x, y, c);
            }
          }
        }
        for (std::uint32_t c = 0; c < components; ++c) {
          mean[c] = static_cast<std::uint16_t>(((2 * sums[c]) + count) / (2 * count));
        }
      }
      for (std::uint32_t y = top; y < bottom; ++y) {
        for (std::uint32_t x = left; x < right; ++x) {
          buffer.set_pixel(x, y, mean);
        }
      }
    }
  }
}

}  // namespace pdfkit::images::treatments
