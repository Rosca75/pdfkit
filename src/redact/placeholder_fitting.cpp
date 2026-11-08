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
#include "text/text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
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

// The share of the font size added to the layout's overprint threshold, so
// a placeholder drawn at a rounded scaling still crosses it.
constexpr double margin = 0.05;
// The device length a bridged placeholder stops short of the next kept word
// at the least: two of the 0.01 pt steps the layout rounds positions to, so
// the bridge starts before that word and is read ahead of it.
constexpr double bridge_minimum = 0.02;

// The device length of one text-space unit along the run's x axis.
[[nodiscard]] double unit_length(const text::GlyphRecord& record) noexcept {
  const text::Matrix::Point vector = placement(record).apply_vector(1.0, 0.0);
  const double length = std::hypot(vector.x, vector.y);
  return std::isfinite(length) ? length : 0.0;
}

}  // namespace

PlaceholderFitting::Result PlaceholderFitting::fit(
    std::span<const text::GlyphRecord* const> removed,
    std::span<const text::GlyphRecord* const> line_kept,
    const std::map<std::size_t, std::size_t>& words, std::string_view placeholder) {
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
  // The end of the removed text's last glyph that is not a space, so a
  // removed space after it counts as the white space it is.
  double ink = start;
  for (const text::GlyphRecord* record : removed) {
    if (!record->is_space()) {
      ink = std::max({ink, axis.along(record->origin), axis.along(record->advance_end)});
    }
  }
  double stop = end;
  const text::GlyphRecord* next = nullptr;
  for (const text::GlyphRecord* record : line_kept) {
    // A kept space is part of the white space the hole runs over.
    if (record->is_space()) {
      continue;
    }
    const double at = axis.along(record->origin);
    if (at >= end - 0.01 && (next == nullptr || at < stop)) {
      stop = at;
      next = record;
    }
  }
  result.anchor = anchor;
  result.face = face_for(*anchor);
  result.hole = std::max(0.0, stop - start);
  result.natural = PlaceholderFont::width(placeholder, result.face) / 1000.0 *
                   std::abs(anchor->font_size) * std::abs(anchor->horizontal_scaling / 100.0) *
                   unit_length(*anchor);
  // Whether the original read a word break between the removed text and the
  // next kept word: a space glyph between them, or the layout's words, which
  // put the next kept glyph in a word holding none of the removed glyphs
  // (the layout's one reading of where words end, so the placeholder reads
  // as the text it replaced did).
  double size = 0.0;
  bool broken = false;
  if (next != nullptr) {
    size = std::max(anchor->size, next->size);
    bool space_between = ink < end - 0.01;
    for (const text::GlyphRecord* record : line_kept) {
      const double at = axis.along(record->origin);
      space_between = space_between || (record->is_space() && at >= ink - 0.01 && at < stop);
    }
    const auto word_of = [&](const text::GlyphRecord& record) -> std::optional<std::size_t> {
      const auto found = words.find(record.draw);
      return found == words.end() ? std::nullopt : std::optional<std::size_t>(found->second);
    };
    const std::optional<std::size_t> next_word = word_of(*next);
    const bool apart = next_word.has_value() &&
                       std::ranges::none_of(removed, [&](const text::GlyphRecord* record) {
                         return word_of(*record) == next_word;
                       });
    broken = space_between || apart;
  }
  // A placeholder the bridge will follow fits into the hole less the
  // bridge's least length.
  const double fill = broken ? std::max(0.0, result.hole - bridge_minimum) : result.hole;
  if (result.natural <= fill || result.natural <= 0.0) {
    result.outcome = Outcome::natural;
    result.scale = 1.0;
  } else if (fill >= result.natural * floor) {
    result.outcome = Outcome::scaled;
    result.scale = fill / result.natural;
  } else {
    result.outcome = Outcome::overflow;
    result.scale = floor;
    // An overflow that would end in the word gap before the next word, or
    // barely over it, reads glued to it: it is drawn wide enough to run
    // over that word's start by an overprint, never wider than natural.
    const double over = result.hole + ((text::TextLayout::overprint_gap + margin) * size);
    if (broken && result.natural * floor < over) {
      result.scale = std::min(1.0, over / result.natural);
    }
  }
  // A placeholder that ends at or before the next kept word is followed by
  // the bridge: a space of the placeholder face whose advance, (w0 * Tfs +
  // Tc) * Th along the run's axis, is the white space left (ISO 32000-1
  // section 9.4.4, read), so Tc takes the sign of the font size.
  const double length = result.natural * result.scale;
  const double th = std::abs(anchor->horizontal_scaling / 100.0 * result.scale);
  const double unit = unit_length(*anchor);
  if (broken && length <= fill + 0.01 && th > 0.0 && unit > 0.0) {
    const double sign = anchor->font_size < 0.0 ? -1.0 : 1.0;
    const double white = std::max(0.0, result.hole - length);
    result.bridge = true;
    result.bridge_spacing = (sign * white / (unit * th)) -
                            (PlaceholderFont::width(" ", result.face) / 1000.0 * anchor->font_size);
  }
  result.box = box(*anchor, placeholder, result.face, result.scale);
  return result;
}

text::Rect PlaceholderFitting::box(const text::GlyphRecord& anchor, std::string_view placeholder,
                                   PlaceholderFont::Face face, double scale) {
  const PlaceholderFont::Metrics& metrics = PlaceholderFont::metrics(face);
  const double size = anchor.font_size;
  const double length = PlaceholderFont::width(placeholder, face) / 1000.0 * size *
                        anchor.horizontal_scaling / 100.0 * scale;
  return text::Rect::transformed(0.0, anchor.rise + (metrics.bbox[1] / 1000.0 * size), length,
                                 anchor.rise + (metrics.bbox[3] / 1000.0 * size),
                                 placement(anchor));
}

PlaceholderFont::Face PlaceholderFitting::face_for(const text::GlyphRecord& anchor) {
  return anchor.font != nullptr && anchor.font->is_bold() ? PlaceholderFont::Face::bold
                                                          : PlaceholderFont::Face::regular;
}

void PlaceholderFitting::require_alphabet(std::string_view placeholder, std::string_view where) {
  const std::optional<std::string> bad = PlaceholderFont::outside(placeholder);
  if (!bad) {
    return;
  }
  const bool ill_formed = !bad->starts_with("U+");
  throw platform::Error(
      platform::ErrorCode::invalid_argument,
      std::string(where) + " has a placeholder that holds " + *bad +
          (ill_formed ? std::string(", which is not text")
                      : ", a character outside the placeholder alphabet"),
      "a placeholder of UTF-8 text over printable ASCII (U+0020 to U+007E), the Latin-1 "
      "Supplement (U+00A0 to U+00FF) and Latin Extended-A (U+0100 to U+017F), which the "
      "embedded placeholder font draws",
      ill_formed ? std::string("send the placeholder as UTF-8")
                 : "write the placeholder without " + *bad +
                       ", for example [REDACTED], a mask of asterisks or a name such as "
                       "Jérôme Dupont");
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
