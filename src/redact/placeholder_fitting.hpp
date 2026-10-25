// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PlaceholderFitting decides how a placeholder fills the hole a match left,
// docs/DESIGN.md section 13, the sizing Acrobat, Apryse and PyMuPDF apply to
// their overlay text (design record, read): the hole runs from the removed
// run's first glyph on its first line to the next kept glyph on that line,
// or to the run's end when nothing follows; the placeholder is drawn at the
// run's font size and horizontal scaling when it fits (natural), else
// narrowed with Tz down to a floor of 60 percent (scaled), else at the floor
// running past the hole to the right (overflow), because an overflow is
// visible and a placeholder squeezed over the next word would hide it. The
// placeholder is drawn bold when the removed glyph's font is bold (the
// descriptor's /FontWeight 600 or more, the ForceBold flag, a Core 14 bold
// face, or "Bold", "Black" or "Heavy" in its name, as fonts::PdfFont reads
// it). Lengths are measured in device space along the run's direction.

#ifndef PDFKIT_REDACT_PLACEHOLDER_FITTING_HPP
#define PDFKIT_REDACT_PLACEHOLDER_FITTING_HPP

#include "text/glyph_record.hpp"
#include "text/rect.hpp"
#include "writer/placeholder_font.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace pdfkit::redact {

class PlaceholderFitting {
 public:
  enum class Outcome : std::uint8_t { natural, scaled, overflow };

  // The narrowest horizontal scaling, as a share of the run's.
  static constexpr double floor = 0.6;

  struct Result {
    Outcome outcome = Outcome::natural;
    // The share of the run's horizontal scaling the placeholder is drawn at.
    double scale = 1.0;
    // The hole and the placeholder's natural length, in device space.
    double hole = 0.0;
    double natural = 0.0;
    // The removed glyph the placeholder starts at.
    const text::GlyphRecord* anchor = nullptr;
    writer::PlaceholderFont::Face face = writer::PlaceholderFont::Face::regular;
    // The device box the drawn placeholder covers (its font's bounding box
    // over its drawn length).
    text::Rect box;
  };

  // Fits placeholder into the hole of removed (the removed glyphs of the
  // run's first line, not empty), with line_kept the kept glyphs of that
  // line.
  [[nodiscard]] static Result fit(std::span<const text::GlyphRecord* const> removed,
                                  std::span<const text::GlyphRecord* const> line_kept,
                                  std::string_view placeholder);

  // The device box of placeholder drawn at anchor with the run's state and
  // scale times its horizontal scaling.
  [[nodiscard]] static text::Rect box(const text::GlyphRecord& anchor, std::string_view placeholder,
                                      writer::PlaceholderFont::Face face, double scale);

  [[nodiscard]] static writer::PlaceholderFont::Face face_for(const text::GlyphRecord& anchor);

  // Throws platform::Error (invalid_argument) naming the first character of
  // placeholder outside the alphabet; where names the match.
  static void require_alphabet(std::string_view placeholder, std::string_view where);

  [[nodiscard]] static std::string_view name(Outcome outcome) noexcept;
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_PLACEHOLDER_FITTING_HPP
