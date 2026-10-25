// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The inverse of the CTM is computed directly from its six numbers. A page
// rectangle's four corners go through it onto the unit square; their bound,
// scaled by the pixel grid, is floored at its low edges and ceiled at its high
// edges with a tolerance of a thousandth of a pixel, so a rectangle that lies
// exactly on pixel edges (a region found in the pixels and mapped back) keeps
// its size instead of growing by a pixel from rounding noise.

#include "images/treatments/region_mapper.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pdfkit::images::treatments {

namespace {

constexpr double tolerance = 1e-3;

std::uint32_t floor_to(double value, std::uint32_t limit) {
  const double floored = std::floor(value + tolerance);
  return static_cast<std::uint32_t>(std::clamp(floored, 0.0, static_cast<double>(limit)));
}

std::uint32_t ceil_to(double value, std::uint32_t limit) {
  const double ceiled = std::ceil(value - tolerance);
  return static_cast<std::uint32_t>(std::clamp(ceiled, 0.0, static_cast<double>(limit)));
}

}  // namespace

std::optional<PixelBuffer::Rect> RegionMapper::to_pixels(const text::Matrix& ctm,
                                                         std::uint32_t width, std::uint32_t height,
                                                         const text::Rect& page) {
  const double det = ctm.determinant();
  if (page.is_empty() || !std::isfinite(det) || std::abs(det) < 1e-12 || width == 0 ||
      height == 0) {
    return std::nullopt;
  }
  // The inverse of [a b c d e f]: x = (d (X - e) - c (Y - f)) / det,
  // y = (a (Y - f) - b (X - e)) / det.
  const auto inverse = [&](double x, double y) {
    const double px = x - ctm.e;
    const double py = y - ctm.f;
    return text::Matrix::Point{((ctm.d * px) - (ctm.c * py)) / det,
                               ((ctm.a * py) - (ctm.b * px)) / det};
  };
  text::Rect square;
  for (const auto& [x, y] : {std::pair{page.left, page.bottom}, std::pair{page.right, page.bottom},
                             std::pair{page.left, page.top}, std::pair{page.right, page.top}}) {
    const text::Matrix::Point point = inverse(x, y);
    square.add(point.x, point.y);
  }
  const double w = width;
  const double h = height;
  PixelBuffer::Rect out{floor_to(square.left * w, width), floor_to((1.0 - square.top) * h, height),
                        ceil_to(square.right * w, width),
                        ceil_to((1.0 - square.bottom) * h, height)};
  if (out.empty()) {
    return std::nullopt;
  }
  return out;
}

std::optional<PixelBuffer::Rect> RegionMapper::clip(const std::array<double, 4>& pixels,
                                                    std::uint32_t width, std::uint32_t height) {
  if (!std::ranges::all_of(pixels, [](double v) { return std::isfinite(v); })) {
    return std::nullopt;
  }
  PixelBuffer::Rect out{floor_to(std::min(pixels[0], pixels[2]), width),
                        floor_to(std::min(pixels[1], pixels[3]), height),
                        ceil_to(std::max(pixels[0], pixels[2]), width),
                        ceil_to(std::max(pixels[1], pixels[3]), height)};
  if (out.empty()) {
    return std::nullopt;
  }
  return out;
}

text::Rect RegionMapper::to_page(const text::Matrix& ctm, std::uint32_t width, std::uint32_t height,
                                 const PixelBuffer::Rect& pixels) {
  if (pixels.empty() || width == 0 || height == 0) {
    return text::Rect::empty();
  }
  const double w = width;
  const double h = height;
  return text::Rect::transformed(pixels.left / w, 1.0 - (pixels.bottom / h), pixels.right / w,
                                 1.0 - (pixels.top / h), ctm);
}

}  // namespace pdfkit::images::treatments
