// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// CaptionDrawer draws a box's caption into the image's samples,
// docs/DESIGN.md section 14: the caption, from the placeholder alphabet ([, ],
// _, 0-9, A-Z), centred in the box at the largest height of the bitmap font
// (src/images/bitmap_font_data.inc, rasterised from Liberation Sans by
// build/fonts/bitmap_font.py) whose line fits inside it, in one ink chosen by
// the caller for contrast with the box. A caption no height fits is not
// drawn, because a cut caption reads as other text. The caption is drawn
// upright as the page shows the image: the orientation names the pixel steps
// that move one caption pixel right and one down on the page, so an image
// drawn flipped or turned by a multiple of 90 degrees gets a caption that
// reads the right way round.

#ifndef PDFKIT_IMAGES_TREATMENTS_CAPTION_DRAWER_HPP
#define PDFKIT_IMAGES_TREATMENTS_CAPTION_DRAWER_HPP

#include "images/treatments/pixel_buffer.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::images::treatments {

class CaptionDrawer {
 public:
  // The pixel step (column, row) of one caption pixel to the right and of one
  // caption pixel down; each a unit step along one axis, the two along
  // different axes.
  struct Orientation {
    int right_x = 1;
    int right_y = 0;
    int down_x = 0;
    int down_y = 1;
  };

  // The characters of caption outside the alphabet, each once, in order.
  [[nodiscard]] static std::string outside(std::string_view caption);

  // Draws caption centred in rect (cut to the image) with ink; returns the
  // pixel height used, or 0 when no height of the font fits (nothing drawn).
  // A character outside the alphabet throws platform::Error
  // (invalid_argument), naming it.
  static std::uint32_t draw(PixelBuffer& buffer, const PixelBuffer::Rect& rect,
                            std::string_view caption, std::span<const std::uint16_t> ink,
                            const Orientation& orientation);

  // The width in pixels of caption at a height of the font, 0 for a height
  // the font lacks.
  [[nodiscard]] static std::uint32_t width(std::string_view caption, std::uint32_t height);

  // The font's heights, ascending.
  [[nodiscard]] static std::span<const std::uint32_t> heights() noexcept;

  // The SHA-256 the generator recorded over the canonical lines, and those
  // lines rebuilt from the compiled table (height, character, width and the
  // rows in hexadecimal), so a test proves the data is what was generated.
  [[nodiscard]] static std::string_view recorded_sha256() noexcept;
  [[nodiscard]] static std::vector<std::string> canonical_lines();
};

}  // namespace pdfkit::images::treatments

#endif  // PDFKIT_IMAGES_TREATMENTS_CAPTION_DRAWER_HPP
