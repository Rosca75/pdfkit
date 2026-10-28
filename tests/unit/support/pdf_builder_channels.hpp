// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The fixture writer's planted-marker knobs: one per channel of the channel
// table (docs/DESIGN.md section 15) and one per channel a page owns or names
// (section 16). Each knob plants a caller-given marker in its channel alone,
// as a literal string or in stream data, so a byte scan of an output with
// every stream decoded finds it exactly when the channel survived. Markers
// read PDFKITMARK<row>X<n>: the row's two-digit number, then a counter.

#ifndef PDFKIT_TESTS_SUPPORT_PDF_BUILDER_CHANNELS_HPP
#define PDFKIT_TESTS_SUPPORT_PDF_BUILDER_CHANNELS_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::test {

class PdfBuilderChannels {
 public:
  // The rows of the channel table, by id, in the table's order; the marker of
  // a row uses its 1-based position here.
  [[nodiscard]] static const std::vector<std::string_view>& rows();

  // "PDFKITMARK<row>X<n>" with both numbers on two digits.
  [[nodiscard]] static std::string marker(int row, int n);

  // The text every planted page draws, which holds no marker.
  static constexpr std::string_view drawn_text = "Planted fixture page";

  // A one-page document with marker planted in the channel of row id alone.
  [[nodiscard]] static std::string planted(std::string_view id, const std::string& marker);

  // The page-selection leak fixture of section 16, pages pages. Page n plants
  // marker(row, n) in every channel it owns or names: drawn content that must
  // survive exactly when the page is kept (90 its content stream, 91 a form
  // only it draws, 92 an image only it draws, 93 an entry of the resource
  // dictionary every page inherits, drawn by page n only), and channels that
  // must never survive (1 an incremental update revised its content, 2 an
  // unreferenced object, 3 a thumbnail, 9 an annotation and its popup, 13 a
  // structure node with /ActualText, 14 an optional-content group only it
  // uses, 15 a page label).
  [[nodiscard]] static std::string leak_fixture(std::uint32_t pages);

  // The off-layer fixture of docs/DESIGN.md section 15: one page with two
  // groups, "on" and "off" (named by marker(14, 1) and marker(14, 2)), whose
  // default configuration turns "off" off. marker(25, n) is planted only in
  // content no viewer shows, and marker(90, n) only in content it shows,
  // after which the page draws drawn_text. The kinds: "bdc" (marked-content
  // sequences, one inside a text object whose shown text follows it), "form"
  // and "image" (XObjects whose /OC is off or on, and an inline image in an
  // off sequence), "nested" (on inside off, off inside on), "base_off"
  // (/BaseState /OFF with "on" listed in /ON) and "membership" (membership
  // dictionaries by name and inline).
  [[nodiscard]] static std::string off_layers(std::string_view kind);
  static constexpr std::array<std::string_view, 6> off_layer_kinds{
      "bdc", "form", "image", "nested", "base_off", "membership"};

  // The rows the leak fixture plants, drawn and stripped.
  static constexpr std::array<int, 4> drawn_rows{90, 91, 92, 93};
  static constexpr std::array<int, 7> stripped_rows{1, 2, 3, 9, 13, 14, 15};
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_SUPPORT_PDF_BUILDER_CHANNELS_HPP
