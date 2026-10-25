// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// RegionMapper moves rectangles between a page and an image's pixel grid.
// An image is drawn as the unit square under the CTM at its draw, and its
// pixel grid lies on that square with the first row at the top: pixel column
// x spans u from x / width to (x + 1) / width, and row y spans v from
// 1 - (y + 1) / height to 1 - y / height (ISO 32000-1 section 8.9.4, recalled;
// the unit test checks it on an identity, a scaled, a turned and a flipped
// matrix). A page rectangle maps through the CTM's inverse onto the square
// and is rounded outward to whole pixels, so every pixel it touches is
// treated; a pixel rectangle maps forward to the bound of its four corners on
// the page, which is the device rectangle a treated region changes.

#ifndef PDFKIT_IMAGES_TREATMENTS_REGION_MAPPER_HPP
#define PDFKIT_IMAGES_TREATMENTS_REGION_MAPPER_HPP

#include "images/treatments/pixel_buffer.hpp"
#include "text/matrix.hpp"
#include "text/rect.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace pdfkit::images::treatments {

class RegionMapper {
 public:
  // The pixels of a width by height image drawn under ctm that the page
  // rectangle covers, cut to the image; no value when it covers none or the
  // CTM is singular.
  [[nodiscard]] static std::optional<PixelBuffer::Rect> to_pixels(const text::Matrix& ctm,
                                                                  std::uint32_t width,
                                                                  std::uint32_t height,
                                                                  const text::Rect& page);

  // A caller's pixel rectangle [left, top, right, bottom] (columns and rows,
  // from the top-left corner, right and bottom exclusive) rounded outward and
  // cut to the image; no value when it covers no pixel.
  [[nodiscard]] static std::optional<PixelBuffer::Rect> clip(const std::array<double, 4>& pixels,
                                                             std::uint32_t width,
                                                             std::uint32_t height);

  // The page bound of a pixel rectangle of a width by height image drawn
  // under ctm.
  [[nodiscard]] static text::Rect to_page(const text::Matrix& ctm, std::uint32_t width,
                                          std::uint32_t height, const PixelBuffer::Rect& pixels);
};

}  // namespace pdfkit::images::treatments

#endif  // PDFKIT_IMAGES_TREATMENTS_REGION_MAPPER_HPP
