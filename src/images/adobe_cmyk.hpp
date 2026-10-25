// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// AdobeCmyk converts a CMYK colour to sRGB the way PDFium does: a 9 by 9 by
// 9 by 9 table of RGB samples over the CMYK cube, interpolated in fixed point
// along each axis. It is a port of PDFium's core/fxge/dib/cfx_cmyk_to_srgb.cpp
// at commit 7b3f6f93625e6df97a7d9e677137a558df8648c4 (read; BSD-3-Clause, Copyright 2014 The PDFium
// Authors, original code copyright 2014 Foxit Software Inc.; the notice is in
// THIRD-PARTY-NOTICES.md), the table and the arithmetic unchanged. It is
// what DeviceCMYK and every CMYK alternate convert through, because the naive
// complement R = (1 - C)(1 - K) differs from PDFium, the dataset's
// independent reader, by a mean of 10 to 20 per channel on real CMYK images,
// and this table agrees with it.

#ifndef PDFKIT_IMAGES_ADOBE_CMYK_HPP
#define PDFKIT_IMAGES_ADOBE_CMYK_HPP

#include <array>
#include <cstdint>

namespace pdfkit::images {

class AdobeCmyk {
 public:
  // The sRGB of an 8-bit CMYK colour.
  [[nodiscard]] static std::array<std::uint8_t, 3> to_rgb(std::uint8_t c, std::uint8_t m,
                                                          std::uint8_t y, std::uint8_t k) noexcept;
  // The same for components in 0..1, rounded to 8 bits first as PDFium rounds
  // them, and the result in 0..1.
  [[nodiscard]] static std::array<double, 3> to_rgb(double c, double m, double y,
                                                    double k) noexcept;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_ADOBE_CMYK_HPP
