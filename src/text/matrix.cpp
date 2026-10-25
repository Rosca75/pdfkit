// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The matrix product in the specification's row-vector convention, and the
// checks the interpreter applies before trusting a matrix read from a file.

#include "text/matrix.hpp"

#include <cmath>

namespace pdfkit::text {

std::optional<Matrix> Matrix::of(const std::array<double, 6>& values) noexcept {
  for (const double value : values) {
    if (!std::isfinite(value)) {
      return std::nullopt;
    }
  }
  return Matrix{values[0], values[1], values[2], values[3], values[4], values[5]};
}

Matrix Matrix::operator*(const Matrix& other) const noexcept {
  return {(a * other.a) + (b * other.c),
          (a * other.b) + (b * other.d),
          (c * other.a) + (d * other.c),
          (c * other.b) + (d * other.d),
          (e * other.a) + (f * other.c) + other.e,
          (e * other.b) + (f * other.d) + other.f};
}

bool Matrix::axis_aligned() const noexcept {
  constexpr double tolerance = 1e-9;
  const bool straight = std::abs(b) <= tolerance && std::abs(c) <= tolerance;
  const bool quarter = std::abs(a) <= tolerance && std::abs(d) <= tolerance;
  return straight || quarter;
}

}  // namespace pdfkit::text
