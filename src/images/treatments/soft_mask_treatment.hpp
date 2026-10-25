// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// SoftMaskTreatment carries a treatment over to an image's mask,
// docs/DESIGN.md section 14: the rectangle treated in the colour samples is
// treated in the soft mask (/SMask, or the alpha of a JPEG 2000 codestream)
// or the stencil mask (/Mask as a stream) too, made opaque under a box and
// mosaicked under a blur, because a box over the colour and not over the mask
// shows the box's edge through the mask's shape, and an untouched mask keeps
// the outline of what was boxed. A mask of another size than its image is
// treated over the same part of the picture: the rectangle is scaled onto
// the mask's grid and rounded outward. A colour-key /Mask has no samples of
// its own; BoxPainter keeps a box's fill out of its ranges.

#ifndef PDFKIT_IMAGES_TREATMENTS_SOFT_MASK_TREATMENT_HPP
#define PDFKIT_IMAGES_TREATMENTS_SOFT_MASK_TREATMENT_HPP

#include "images/treatments/box_painter.hpp"
#include "images/treatments/pixel_buffer.hpp"

#include <cstdint>

namespace pdfkit::images::treatments {

class SoftMaskTreatment {
 public:
  // rect of a width by height image scaled onto mask's grid, rounded outward.
  [[nodiscard]] static PixelBuffer::Rect scale(const PixelBuffer::Rect& rect, std::uint32_t width,
                                               std::uint32_t height, const PixelBuffer& mask);

  // Under a box: the mask inside rect (on the image's grid) becomes opaque.
  // meaning is the mask's: a soft mask's grey through its /Decode (opaque is
  // the value nearest to 1), or a stencil (opaque is the value that paints).
  static void opaque(PixelBuffer& mask, const PixelBuffer::Rect& rect, std::uint32_t width,
                     std::uint32_t height, const BoxPainter::Meaning& meaning);

  // Under a blur: the mask inside rect is mosaicked with the image's block,
  // scaled to the mask's grid (never under the blur's minimum).
  static void blur(PixelBuffer& mask, const PixelBuffer::Rect& rect, std::uint32_t width,
                   std::uint32_t height, std::uint32_t block, const BoxPainter::Meaning& meaning);
};

}  // namespace pdfkit::images::treatments

#endif  // PDFKIT_IMAGES_TREATMENTS_SOFT_MASK_TREATMENT_HPP
