// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The hidden-text classes of CLAUDE.md section 4 and docs/DESIGN.md chapter
// 8, as bits of a mask, because one glyph can belong to several (white and
// clipped, say) and each is reported. The names are the stable strings the
// JSON and the hidden_text warning write.

#ifndef PDFKIT_TEXT_HIDDEN_CLASS_HPP
#define PDFKIT_TEXT_HIDDEN_CLASS_HPP

#include <array>
#include <cstdint>
#include <string_view>

namespace pdfkit::text {

enum class HiddenClass : std::uint8_t {
  render_invisible = 1U << 0U,
  white_fill = 1U << 1U,
  clipped = 1U << 2U,
  off_media = 1U << 3U,
  off_crop = 1U << 4U,
  optional_content_off = 1U << 5U,
  duplicate = 1U << 6U,
  covered = 1U << 7U,
};

// Every class in the order reports list them.
inline constexpr std::array<HiddenClass, 8> hidden_classes{
    HiddenClass::render_invisible, HiddenClass::white_fill, HiddenClass::clipped,
    HiddenClass::off_media,        HiddenClass::off_crop,   HiddenClass::optional_content_off,
    HiddenClass::duplicate,        HiddenClass::covered};

[[nodiscard]] constexpr std::uint16_t hidden_bit(HiddenClass value) noexcept {
  return static_cast<std::uint16_t>(value);
}

[[nodiscard]] constexpr std::string_view hidden_class_name(HiddenClass value) noexcept {
  switch (value) {
    case HiddenClass::render_invisible:
      return "render_invisible";
    case HiddenClass::white_fill:
      return "white_fill";
    case HiddenClass::clipped:
      return "clipped";
    case HiddenClass::off_media:
      return "off_media";
    case HiddenClass::off_crop:
      return "off_crop";
    case HiddenClass::optional_content_off:
      return "optional_content_off";
    case HiddenClass::duplicate:
      return "duplicate";
    case HiddenClass::covered:
      return "covered";
  }
  return "covered";
}

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_HIDDEN_CLASS_HPP
