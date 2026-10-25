// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Matrix is the affine transformation of ISO 32000-1 section 8.3.3 (read):
// [a b c d e f] maps a point (x, y) to (a x + c y + e, b x + d y + f), and a
// product A * B applies A first, then B, which is the order the
// specification writes Trm = Tparams x Tm x CTM in. The interpreter keeps the
// CTM, the text matrix and the line matrix in this form, and every glyph
// record and image occurrence carries the matrices it was drawn under, so a
// later edit can place a glyph exactly where the original placed it.

#ifndef PDFKIT_TEXT_MATRIX_HPP
#define PDFKIT_TEXT_MATRIX_HPP

#include <array>
#include <optional>

namespace pdfkit::text {

class Matrix {
 public:
  double a = 1.0;
  double b = 0.0;
  double c = 0.0;
  double d = 1.0;
  double e = 0.0;
  double f = 0.0;

  // The point (x, y) under this matrix.
  struct Point {
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const Point&, const Point&) = default;
  };

  [[nodiscard]] static Matrix identity() noexcept { return {}; }
  [[nodiscard]] static Matrix translation(double x, double y) noexcept {
    return {1.0, 0.0, 0.0, 1.0, x, y};
  }

  // The matrix of six operands; no value when one is not finite.
  [[nodiscard]] static std::optional<Matrix> of(const std::array<double, 6>& values) noexcept;

  // This matrix applied first, then other.
  [[nodiscard]] Matrix operator*(const Matrix& other) const noexcept;

  [[nodiscard]] Point apply(double x, double y) const noexcept {
    return {(a * x) + (c * y) + e, (b * x) + (d * y) + f};
  }
  // A direction (no translation) under this matrix.
  [[nodiscard]] Point apply_vector(double x, double y) const noexcept {
    return {(a * x) + (c * y), (b * x) + (d * y)};
  }

  [[nodiscard]] double determinant() const noexcept { return (a * d) - (b * c); }
  // Whether the matrix maps axis-aligned rectangles to axis-aligned
  // rectangles (no rotation other than multiples of 90 degrees, no skew).
  [[nodiscard]] bool axis_aligned() const noexcept;

  [[nodiscard]] std::array<double, 6> values() const noexcept { return {a, b, c, d, e, f}; }

  friend bool operator==(const Matrix&, const Matrix&) = default;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_MATRIX_HPP
