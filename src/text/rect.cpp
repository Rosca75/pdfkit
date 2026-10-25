// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Rectangle arithmetic. An empty rectangle intersects nothing and is
// contained by everything, so a glyph with a degenerate box is never called
// covered or clipped by an accident of its emptiness.

#include "text/rect.hpp"

#include <algorithm>
#include <cmath>

namespace pdfkit::text {

Rect Rect::everything() noexcept {
  constexpr double inf = std::numeric_limits<double>::infinity();
  return {-inf, -inf, inf, inf};
}

Rect Rect::of(double x0, double y0, double x1, double y1) noexcept {
  return {std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
}

Rect Rect::transformed(double x0, double y0, double x1, double y1, const Matrix& matrix) noexcept {
  Rect out;
  const Matrix::Point p0 = matrix.apply(x0, y0);
  const Matrix::Point p1 = matrix.apply(x1, y0);
  const Matrix::Point p2 = matrix.apply(x1, y1);
  const Matrix::Point p3 = matrix.apply(x0, y1);
  out.add(p0.x, p0.y);
  out.add(p1.x, p1.y);
  out.add(p2.x, p2.y);
  out.add(p3.x, p3.y);
  return out;
}

bool Rect::is_infinite() const noexcept {
  return std::isinf(left) || std::isinf(bottom) || std::isinf(right) || std::isinf(top);
}

void Rect::add(double x, double y) noexcept {
  if (!std::isfinite(x) || !std::isfinite(y)) {
    return;
  }
  left = std::min(left, x);
  bottom = std::min(bottom, y);
  right = std::max(right, x);
  top = std::max(top, y);
}

void Rect::add(const Rect& other) noexcept {
  if (other.is_empty()) {
    return;
  }
  left = std::min(left, other.left);
  bottom = std::min(bottom, other.bottom);
  right = std::max(right, other.right);
  top = std::max(top, other.top);
}

Rect Rect::intersection(const Rect& other) const noexcept {
  return {std::max(left, other.left), std::max(bottom, other.bottom),
          std::min(right, other.right), std::min(top, other.top)};
}

bool Rect::intersects(const Rect& other) const noexcept {
  if (is_empty() || other.is_empty()) {
    return false;
  }
  return other.left <= right && other.right >= left && other.bottom <= top && other.top >= bottom;
}

bool Rect::contains(const Rect& other, double tolerance) const noexcept {
  if (other.is_empty()) {
    return true;
  }
  if (is_empty()) {
    return false;
  }
  return other.left >= left - tolerance && other.right <= right + tolerance &&
         other.bottom >= bottom - tolerance && other.top <= top + tolerance;
}

bool Rect::excludes(const Rect& other, double tolerance) const noexcept {
  if (is_empty() || other.is_empty()) {
    return false;
  }
  return other.left >= right - tolerance || other.right <= left + tolerance ||
         other.bottom >= top - tolerance || other.top <= bottom + tolerance;
}

}  // namespace pdfkit::text
