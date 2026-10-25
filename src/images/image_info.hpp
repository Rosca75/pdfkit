// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageInfo is an image's description as a listing states it, ISO 32000-1
// section 8.9.5 (recalled): the encoding (the last filter of its chain, or
// "none"), the filters in order, /Width, /Height, /BitsPerComponent, the
// colour space's name and component count, /ImageMask, /Interpolate and the
// kind of mask it carries. It reads an image XObject's dictionary or an
// inline image's, with the inline abbreviations of keys and names expanded,
// and never decodes the data, so a listing describes even an image that does
// not decode.

#ifndef PDFKIT_IMAGES_IMAGE_INFO_HPP
#define PDFKIT_IMAGES_IMAGE_INFO_HPP

#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::images {

class ImageInfo {
 public:
  std::string encoding = "none";
  std::vector<std::string> filters;
  std::optional<std::int64_t> width;
  std::optional<std::int64_t> height;
  std::optional<std::int64_t> bits_per_component;
  // The colour space's family name ("DeviceRGB", "ICCBased", "Indexed"),
  // "invalid" for one that does not parse, empty when the image states none.
  std::string colour_space;
  std::optional<std::size_t> components;
  bool image_mask = false;
  bool interpolate = false;
  // "none", "smask", "stencil", "color_key" or "smask_in_data" (Masks::name).
  std::string mask = "none";

  // The description of an image dictionary; colour_spaces is the /ColorSpace
  // resource dictionary an inline image's named space is looked up in (null
  // for an XObject).
  [[nodiscard]] static ImageInfo of(syntax::Document& document,
                                    const syntax::Dictionary& dictionary,
                                    const syntax::Object& colour_spaces = syntax::Object());
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_IMAGE_INFO_HPP
