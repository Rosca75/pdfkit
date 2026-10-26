// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The scaling onto the mask's grid: a column x of the image covers the mask's
// columns from x * mask width / width to (x + 1) * mask width / width, so the
// rectangle's left and top edges are floored and its right and bottom edges
// ceiled; a mask of the image's size keeps the rectangle as it is.

#include "images/treatments/soft_mask_treatment.hpp"

#include "images/treatments/mosaic_blur.hpp"

#include <algorithm>

namespace pdfkit::images::treatments {

namespace {

std::uint32_t scaled_floor(std::uint32_t value, std::uint32_t to, std::uint32_t from) {
  return static_cast<std::uint32_t>((std::uint64_t{value} * to) / from);
}

std::uint32_t scaled_ceil(std::uint32_t value, std::uint32_t to, std::uint32_t from) {
  return static_cast<std::uint32_t>(((std::uint64_t{value} * to) + from - 1) / from);
}

}  // namespace

PixelBuffer::Rect SoftMaskTreatment::scale(const PixelBuffer::Rect& rect, std::uint32_t width,
                                           std::uint32_t height, const PixelBuffer& mask) {
  if (width == 0 || height == 0 || rect.empty()) {
    return {};
  }
  return mask.clip({scaled_floor(rect.left, mask.width(), width),
                    scaled_floor(rect.top, mask.height(), height),
                    scaled_ceil(rect.right, mask.width(), width),
                    scaled_ceil(rect.bottom, mask.height(), height)});
}

void SoftMaskTreatment::opaque(PixelBuffer& mask, const PixelBuffer::Rect& rect,
                               std::uint32_t width, std::uint32_t height,
                               const BoxPainter::Meaning& meaning) {
  // A stencil mask paints where its sample reads 0 (black); a soft mask is
  // opaque where it reads 1 (white).
  const BoxPainter::Colour target =
      meaning.space ? BoxPainter::Colour{1.0, 1.0, 1.0} : BoxPainter::Colour{0.0, 0.0, 0.0};
  const std::vector<std::uint16_t> fill = BoxPainter::nearest(meaning, target);
  BoxPainter::paint(mask, scale(rect, width, height, mask), fill);
}

void SoftMaskTreatment::blur(PixelBuffer& mask, const PixelBuffer::Rect& rect, std::uint32_t width,
                             std::uint32_t height, std::uint32_t block,
                             const BoxPainter::Meaning& meaning) {
  const std::uint32_t scaled =
      std::max({MosaicBlur::minimum_block, scaled_ceil(block, mask.width(), width),
                scaled_ceil(block, mask.height(), height)});
  MosaicBlur::apply(mask, scale(rect, width, height, mask), scaled, meaning);
}

}  // namespace pdfkit::images::treatments
