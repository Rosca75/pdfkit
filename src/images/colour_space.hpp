// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ColourSpace is an image's colour space of ISO 32000-1 section 8.6
// (recalled), docs/DESIGN.md section 9.4: DeviceGray, DeviceRGB, DeviceCMYK;
// CalGray and CalRGB read as their device equivalents; ICCBased read by its
// /N with its /Alternate and no colour management; Lab converted from its
// white point to sRGB; Indexed (base, hival clamped to 255 and to the lookup,
// lookup string or stream); Separation and DeviceN through their tint
// transform into the alternate space. Pattern is refused for an image.
// Every space converts a pixel's components to grey or RGB in 0..1. CMYK
// becomes RGB through PDFium's Adobe CMYK table (AdobeCmyk), not the naive
// complement docs/DESIGN.md section 9.4 named, because the complement differs
// from the independent reader by 10 to 20 per channel on real CMYK images
// (OPEN-QUESTIONS.md section 2 asks the owner to confirm). No colour
// management is applied. A space the file states wrongly throws DecodeFault.

#ifndef PDFKIT_IMAGES_COLOUR_SPACE_HPP
#define PDFKIT_IMAGES_COLOUR_SPACE_HPP

#include "images/pdf_function.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pdfkit::images {

class ColourSpace {
 public:
  enum class Family : std::uint8_t {
    device_gray,
    device_rgb,
    device_cmyk,
    lab,
    indexed,
    separation,
    device_n,
  };

  // The space an object names: a name (DeviceGray, the inline abbreviations
  // G, RGB, CMYK and I, or a name of the colour_spaces resource dictionary,
  // which may be null) or an array. Throws DecodeFault for a space an image
  // cannot use.
  [[nodiscard]] static std::shared_ptr<const ColourSpace> parse(
      syntax::Document& document, const syntax::Object& object,
      const syntax::Object& colour_spaces = syntax::Object(), int depth = 0);

  // A device space of n components (1, 3 or 4), as a JPEG or JPEG 2000
  // codestream implies when the dictionary names none.
  [[nodiscard]] static std::shared_ptr<const ColourSpace> device(std::size_t components);

  [[nodiscard]] Family family() const noexcept { return family_; }
  // The component count of a pixel in this space.
  [[nodiscard]] std::size_t components() const noexcept { return components_; }
  // 1 (grey) or 3 (RGB): what convert writes.
  [[nodiscard]] std::size_t output_channels() const noexcept { return output_channels_; }
  // Whether the space is CMYK at its root (DeviceCMYK or a four-component
  // ICCBased), so its samples are kept for re-encoding.
  [[nodiscard]] bool is_cmyk() const noexcept { return family_ == Family::device_cmyk; }
  // The highest index of an Indexed space.
  [[nodiscard]] std::uint32_t hival() const noexcept { return hival_; }
  // The name for listings: "DeviceRGB", "ICCBased", "Indexed", ...
  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  // The default /Decode array of an image of bits per component in this
  // space (two values per component).
  [[nodiscard]] std::vector<double> default_decode(std::uint32_t bits) const;

  // Converts one pixel's components (already through /Decode) into grey or
  // RGB values in 0..1.
  void convert(std::span<const double> in, std::span<double> out) const;

 private:
  ColourSpace() = default;

  Family family_ = Family::device_gray;
  std::size_t components_ = 1;
  std::size_t output_channels_ = 1;
  std::string name_ = "DeviceGray";
  // ICCBased and Lab: the per-component range.
  std::vector<double> range_;
  // Lab: the white point.
  std::array<double, 3> white_{0.9505, 1.0, 1.089};
  // Indexed: the base and its lookup table, (hival + 1) * base components bytes.
  std::shared_ptr<const ColourSpace> base_;
  std::uint32_t hival_ = 0;
  std::vector<std::uint8_t> lookup_;
  // Separation and DeviceN: the alternate space and the tint transform; a
  // Separation /None paints nothing and reads as white.
  std::shared_ptr<const PdfFunction> tint_;
  bool none_ = false;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_COLOUR_SPACE_HPP
