// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The sample accessor of PixelImage.

#include "images/pixel_image.hpp"

namespace pdfkit::images {

std::uint8_t PixelImage::sample8(std::uint32_t x, std::uint32_t y,
                                 std::uint32_t channel) const noexcept {
  const std::size_t index =
      ((static_cast<std::size_t>(y) * width + x) * channels + channel) * bytes_per_sample();
  if (index >= samples.size()) {
    return 0;
  }
  // The most significant byte of a 16-bit sample is its 8-bit value.
  return samples[index];
}

}  // namespace pdfkit::images
