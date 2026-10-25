// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PngWriter writes a PixelImage as a PNG file (the PNG specification, W3C,
// recalled): grey, grey with alpha, RGB, RGB with alpha; 1, 8 and 16 bits; a
// tRNS chunk for a stencil's transparent grey. The bytes depend on the pixels
// alone: zlib deflate at level 6 with the default strategy, the filter type
// chosen by a fixed rule (None for a 1-bit image, Paeth for every row
// otherwise), and no tIME, tEXt or gamma chunk, so two runs write the same
// file.

#ifndef PDFKIT_IMAGES_PNG_WRITER_HPP
#define PDFKIT_IMAGES_PNG_WRITER_HPP

#include "images/pixel_image.hpp"

#include <cstdint>
#include <vector>

namespace pdfkit::images {

class PngWriter {
 public:
  [[nodiscard]] static std::vector<std::uint8_t> write(const PixelImage& image);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_PNG_WRITER_HPP
