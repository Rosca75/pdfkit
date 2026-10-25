// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The fitting arithmetic. The direction of a run is the device image of its
// text space x axis, turned round when the font size or the scaling is
// negative (the text then runs backwards); every glyph is placed on that
// axis by the projection of its origin, so the run's first glyph is the one
// nearest its start whatever order the content drew them in.

#include "redact/placeholder_fitting.hpp"

#include "platform/error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace pdfkit::redact {

namespace {

using writer::PlaceholderFont;

struct Axis {
  double x = 1.0;
  double y = 0.0;
  [[nodiscard]] double along(const text::Matrix::Point& point) const noexcept {
    return (point.x * x) + (point.y * y);
  }
};

[[nodiscard]] text::Matrix placement(const text::GlyphRecord& record) noexcept {
  return record.text_matrix * record.ctm;
}

[[nodiscard]] Axis axis_of(const text::GlyphRecord& record) noexcept {
  const double sign = record.font_size * record.horizontal_scaling < 0.0 ? -1.0 : 1.0;
  const text::Matrix::Point vector = placement(record).apply_vector(sign, 0.0);
  const double length = std::hypot(vector.x, vector.y);
  if (length == 0.0 || !std::isfinite(length)) {
    return {};
  }
  return {vector.x / length, vector.y / length};
}

// The device length of one text-space unit along the run's x axis.
[[nodiscard]] double unit_length(const text::GlyphRecord& record) noexcept {
  const text::Matrix::Point vector = placement(record).apply_vector(1.0, 0.0);
  const double length = std::hypot(vector.x, vector.y);
  return std::isfinite(length) ? length : 0.0;
}

}  // namespace

PlaceholderFitting::Result PlaceholderFitting::fit(
    std::span<const text::GlyphRecord* const> removed,
    std::span<const text::GlyphRecord* const> line_kept, std::string_view placeholder) {
  Result result;
  if (removed.empty()) {
    return result;
  }
  const Axis axis = axis_of(*removed.front());
  const text::GlyphRecord* anchor = removed.front();
  double end = -std::numeric_limits<double>::infinity();
  for (const text::GlyphRecord* record : removed) {
    if (axis.along(record->origin) < axis.along(anchor->origin)) {
      anchor = record;
    }
    end = std::max({end, axis.along(record->origin), axis.along(record->advance_end)});
  }
  const double start = axis.along(anchor->origin);
  double stop = end;
  bool found = false;
  for (const text::GlyphRecord* record : line_kept) {
    // A kept space is part of the white space the hole runs over.
    if (record->is_space()) {
      continue;
    }
    const double at = axis.along(record->origin);
    if (at >= end - 0.01 && (!found || at < stop)) {
      stop = at;
      found = true;
    }
  }
  result.anchor = anchor;
  result.face = face_for(*anchor);
  result.hole = std::max(0.0, stop - start);
  result.natural = PlaceholderFont::width(placeholder, result.face) / 1000.0 *
                   std::abs(anchor->font_size) * std::abs(anchor->horizontal_scaling / 100.0) *
                   unit_length(*anchor);
  if (result.natural <= result.hole || result.natural <= 0.0) {
    result.outcome = Outcome::natural;
    result.scale = 1.0;
  } else if (result.hole >= result.natural * floor) {
    result.outcome = Outcome::scaled;
    result.scale = result.hole / result.natural;
  } else {
    result.outcome = Outcome::overflow;
    result.scale = floor;
  }
  result.box = box(*anchor, placeholder, result.face, result.scale);
  return result;
}

text::Rect PlaceholderFitting::box(const text::GlyphRecord& anchor, std::string_view placeholder,
                                   PlaceholderFont::Face face, double scale) {
  const PlaceholderFont::Metrics& metrics = PlaceholderFont::metrics(face);
  const double size = anchor.font_size;
  const double length =
      PlaceholderFont::width(placeholder, face) / 1000.0 * size * anchor.horizontal_scaling / 100.0 *
      scale;
  return text::Rect::transformed(0.0, anchor.rise + (metrics.bbox[1] / 1000.0 * size), length,
                                 anchor.rise + (metrics.bbox[3] / 1000.0 * size),
                                 placement(anchor));
}

PlaceholderFont::Face PlaceholderFitting::face_for(const text::GlyphRecord& anchor) {
  return anchor.font != nullptr && anchor.font->is_bold() ? PlaceholderFont::Face::bold
                                                          : PlaceholderFont::Face::regular;
}

void PlaceholderFitting::require_alphabet(std::string_view placeholder, std::string_view where) {
  if (const auto bad = PlaceholderFont::outside(placeholder)) {
    const auto byte = static_cast<unsigned char>(*bad);
    const std::string shown = byte >= 0x20 && byte < 0x7F
                                  ? "'" + std::string(1, *bad) + "'"
                                  : "the byte " + std::to_string(static_cast<unsigned>(byte));
    throw platform::Error(
        platform::ErrorCode::invalid_argument,
        std::string(where) + " has the placeholder \"" + std::string(placeholder) +
            "\", which holds " + shown + ", a character outside the placeholder alphabet",
        "a placeholder of the characters [, ], _, 0 to 9 and A to Z, which the embedded "
        "placeholder font draws",
        "write the placeholder in capitals, digits, brackets and underscores, for example "
        "[REDACTED] or [NAME_1]");
  }
}

std::string_view PlaceholderFitting::name(Outcome outcome) noexcept {
  switch (outcome) {
    case Outcome::natural:
      return "natural";
    case Outcome::scaled:
      return "scaled";
    case Outcome::overflow:
      return "overflow";
  }
  return "natural";
}

}  // namespace pdfkit::redact
