// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// BoxPainter fills a rectangle of an image's samples with one colour,
// docs/DESIGN.md section 14: the box of a box treatment, black unless the
// caller names another colour. The fill is expressed in the source's own
// samples, so the re-encoded image keeps its colour space and depth: the
// painter searches the sample values for the tuple whose colour, read through
// /Decode and the colour space exactly as the decoder reads it, comes nearest
// to the asked colour. That inverts /Decode (a [1 0] image is filled with the
// value that reads black), picks the nearest palette entry of an Indexed
// space, and finds black in any space the decoder converts (a CMYK, Lab,
// ICCBased, Separation or DeviceN image) without an inverse of its colour
// conversion. A tuple that a colour-key /Mask masks out is never chosen,
// because a box whose fill the mask hides shows the original's edge through
// a hole. For a stencil (/ImageMask), whose samples carry no colour, the
// value that paints reads black and the other white.

#ifndef PDFKIT_IMAGES_TREATMENTS_BOX_PAINTER_HPP
#define PDFKIT_IMAGES_TREATMENTS_BOX_PAINTER_HPP

#include "images/colour_space.hpp"
#include "images/treatments/pixel_buffer.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace pdfkit::images::treatments {

class BoxPainter {
 public:
  // A colour as red, green and blue in 0..1 (grey is three equal values).
  using Colour = std::array<double, 3>;

  // What an image's samples mean.
  struct Meaning {
    // The colour space; null for a stencil.
    std::shared_ptr<const ColourSpace> space;
    // Two numbers per component: the /Decode array, the default filled in.
    std::vector<double> decode;
    std::uint32_t bits = 8;
    std::uint32_t components = 1;
    // The colour-key /Mask as raw value ranges, two per component; empty when
    // the image has none.
    std::vector<std::uint32_t> colour_key;
  };

  // The colour samples read as.
  [[nodiscard]] static Colour colour_of(const Meaning& meaning,
                                        std::span<const std::uint16_t> samples);

  // The sample tuple nearest to target that no colour key masks.
  [[nodiscard]] static std::vector<std::uint16_t> nearest(const Meaning& meaning,
                                                          const Colour& target);

  // The tuple nearest to white or the tuple nearest to black, whichever
  // reads farther from the colour of fill (white on a tie), for a caption
  // drawn on a box of that fill.
  [[nodiscard]] static std::vector<std::uint16_t> contrast(const Meaning& meaning,
                                                           std::span<const std::uint16_t> fill);

  // Sets every sample inside rect (cut to the image) to fill.
  static void paint(PixelBuffer& buffer, const PixelBuffer::Rect& rect,
                    std::span<const std::uint16_t> fill);
};

}  // namespace pdfkit::images::treatments

#endif  // PDFKIT_IMAGES_TREATMENTS_BOX_PAINTER_HPP
