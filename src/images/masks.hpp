// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Masks holds the rules of an image's masks, ISO 32000-1 sections 8.9.6 and
// 11.6.5 (recalled), docs/DESIGN.md section 9.4: which mask an image has
// (/SMask, a /Mask stencil stream, a /Mask colour-key array, or the alpha a
// JPEG 2000 codestream carries under /SMaskInData), the colour-key match on
// raw sample values, and the /Matte un-premultiplication of a soft mask's
// image. A mask whose size differs from its image is kept at its own size and
// never resampled, because resampling invents pixels; /Matte then does not
// apply, since no pixel of the mask lies over a pixel of the image.

#ifndef PDFKIT_IMAGES_MASKS_HPP
#define PDFKIT_IMAGES_MASKS_HPP

#include "images/pixel_image.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace pdfkit::images {

class Masks {
 public:
  enum class Kind : std::uint8_t { none, soft, stencil, colour_key, in_data };

  // The mask an image dictionary names, in the order a viewer applies them:
  // /SMask wins over /Mask (section 11.6.5.3), and /SMaskInData counts only for
  // a JPXDecode image without /SMask.
  [[nodiscard]] static Kind kind(syntax::Document& document, const syntax::Dictionary& image,
                                 bool jpx);

  // The name of a kind for listings: "none", "smask", "stencil", "color_key",
  // "smask_in_data".
  [[nodiscard]] static std::string_view name(Kind kind) noexcept;

  // The /Mask colour-key ranges as raw sample values, two per component, or
  // empty when the array is unusable.
  [[nodiscard]] static std::vector<std::uint32_t> colour_key(syntax::Document& document,
                                                             const syntax::Object& mask,
                                                             std::size_t components);

  // Whether a pixel's raw values fall inside every range of a colour key
  // (such a pixel is masked out).
  [[nodiscard]] static bool keyed(std::span<const std::uint16_t> raw,
                                  std::span<const std::uint32_t> ranges) noexcept;

  // Removes the premultiplication by /Matte from image where mask covers it
  // pixel for pixel: c = m + (c - m) / a for every a above zero. Nothing is
  // done when the sizes differ or matte has the wrong length.
  static void unmatte(PixelImage& image, const PixelImage& mask, std::span<const double> matte);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_MASKS_HPP
