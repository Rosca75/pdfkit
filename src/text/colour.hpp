// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Colour is a fill or stroke colour as the content set it, docs/DESIGN.md
// section 6.1: the colour space as named (DeviceRGB, or a resource name such
// as /CS0), the family it resolves to, its component count and the
// components. Colours are values, never converted, because the glyph record
// reports what the content says and the only judgement the text layer makes
// is whether a colour paints white (docs/DESIGN.md chapter 8): gray 1, RGB 1 1
// 1, CMYK 0 0 0 0, and the same readings of an ICCBased space by its /N, a
// calibrated space by its device kin, a Separation or DeviceN at tint zero
// (no ink) and a Lab colour at full lightness.

#ifndef PDFKIT_TEXT_COLOUR_HPP
#define PDFKIT_TEXT_COLOUR_HPP

#include <string>
#include <vector>

namespace pdfkit::text {

class Colour {
 public:
  // The name the content used: a device family name or a resource name.
  std::string space = "DeviceGray";
  // DeviceGray, DeviceRGB, DeviceCMYK, CalGray, CalRGB, Lab, ICCBased,
  // Indexed, Pattern, Separation, DeviceN, or empty for a space that did not
  // resolve.
  std::string family = "DeviceGray";
  // The components the family takes (an ICCBased space's /N, a DeviceN's
  // colorant count); 0 when unknown.
  int count = 1;
  // Empty in a value nothing has set; the graphics state starts each colour
  // at black (gray 0).
  std::vector<double> components;

  // The initial colour of a space: black for the device families, tint 1 for
  // Separation and DeviceN, index 0 for Indexed (ISO 32000-1 section 8.6.8,
  // table 74, read).
  void reset();
  // Whether this colour paints white on a white page.
  [[nodiscard]] bool is_white() const noexcept;

  friend bool operator==(const Colour&, const Colour&) = default;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_COLOUR_HPP
