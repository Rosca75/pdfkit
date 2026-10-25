// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Rect is an axis-aligned rectangle in device space (the page's default user
// space, points, y upward), the form every box of the text layer takes: a
// glyph's box, a line's, a clip bound, a figure's, an image occurrence's. A
// rectangle is empty when nothing has been added to it; the clip rectangle
// "everything" is the infinite rectangle, so intersecting with it changes
// nothing. Rounding to 0.01 pt is applied where a box is compared or written
// (CLAUDE.md section 9), never here.

#ifndef PDFKIT_TEXT_RECT_HPP
#define PDFKIT_TEXT_RECT_HPP

#include "text/matrix.hpp"

#include <limits>

namespace pdfkit::text {

class Rect {
 public:
  double left = std::numeric_limits<double>::infinity();
  double bottom = std::numeric_limits<double>::infinity();
  double right = -std::numeric_limits<double>::infinity();
  double top = -std::numeric_limits<double>::infinity();

  // The rectangle holding nothing; adding a point makes it that point.
  [[nodiscard]] static Rect empty() noexcept { return {}; }
  // The rectangle holding everything, the clip before any W.
  [[nodiscard]] static Rect everything() noexcept;
  [[nodiscard]] static Rect of(double x0, double y0, double x1, double y1) noexcept;
  // The bound of the rectangle [x0 y0 x1 y1] under matrix.
  [[nodiscard]] static Rect transformed(double x0, double y0, double x1, double y1,
                                        const Matrix& matrix) noexcept;

  [[nodiscard]] bool is_empty() const noexcept { return left > right || bottom > top; }
  [[nodiscard]] bool is_infinite() const noexcept;
  [[nodiscard]] double width() const noexcept { return is_empty() ? 0.0 : right - left; }
  [[nodiscard]] double height() const noexcept { return is_empty() ? 0.0 : top - bottom; }

  void add(double x, double y) noexcept;
  void add(const Rect& other) noexcept;

  [[nodiscard]] Rect intersection(const Rect& other) const noexcept;
  // Whether the two share a point (touching edges count).
  [[nodiscard]] bool intersects(const Rect& other) const noexcept;
  // Whether other lies wholly inside this rectangle, within tolerance.
  [[nodiscard]] bool contains(const Rect& other, double tolerance = 0.0) const noexcept;
  // Whether other lies wholly outside this rectangle: no point of it inside,
  // beyond tolerance.
  [[nodiscard]] bool excludes(const Rect& other, double tolerance = 0.0) const noexcept;

  friend bool operator==(const Rect&, const Rect&) = default;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_RECT_HPP
