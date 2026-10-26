// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// MosaicBlur is the blur treatment, docs/DESIGN.md section 14: the image is
// cut into square blocks from its top-left corner, each block at least 8
// pixels and at least a sixteenth of the image's smaller dimension on a side
// (a whole number of 8-pixel JPEG blocks for a JPEG, so the re-encoded image
// keeps every block constant), and every block inside the rectangle is
// filled with its mean, because a mosaic of that size holds
// no recoverable detail where a convolution blur can be inverted. The mean is
// taken per component on the raw samples (the decode and colour conversions
// are monotone per component), except in an Indexed space, whose samples are
// palette positions: there the block takes the palette entry nearest to the
// mean of its colours. A bitonal image is refused below 128 pixels on its
// smaller side, where a block of 8 pixels averages too few samples to hide a
// shape, and a box is offered instead.

#ifndef PDFKIT_IMAGES_TREATMENTS_MOSAIC_BLUR_HPP
#define PDFKIT_IMAGES_TREATMENTS_MOSAIC_BLUR_HPP

#include "images/treatments/box_painter.hpp"
#include "images/treatments/pixel_buffer.hpp"

#include <cstdint>
#include <string_view>

namespace pdfkit::images::treatments {

class MosaicBlur {
 public:
  // The smallest block side, and the smaller side under which a bitonal
  // image is refused.
  static constexpr std::uint32_t minimum_block = 8;
  static constexpr std::uint32_t minimum_bitonal_side = 128;

  // The block side for an image of width by height; jpeg rounds it up to a
  // multiple of 8.
  [[nodiscard]] static std::uint32_t block_size(std::uint32_t width, std::uint32_t height,
                                                bool jpeg) noexcept;

  // Throws platform::Error (invalid_argument) naming what and offering a box
  // when a bitonal image is too small to blur.
  static void require_blurrable(bool bitonal, std::uint32_t width, std::uint32_t height,
                                std::string_view what);

  // Fills the part of every block inside rect (cut to the image) with its
  // mean; the grid starts at the image's top-left corner. meaning tells
  // an Indexed space apart; its other spaces average the raw samples.
  static void apply(PixelBuffer& buffer, const PixelBuffer::Rect& rect, std::uint32_t block,
                    const BoxPainter::Meaning& meaning);
};

}  // namespace pdfkit::images::treatments

#endif  // PDFKIT_IMAGES_TREATMENTS_MOSAIC_BLUR_HPP
