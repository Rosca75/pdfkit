// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The caption's layout: the glyphs of one height sit side by side, each its
// advance wide, all on the cell's baseline, so the line is the sum of the
// advances wide and one cell high. The largest height whose line fits the box
// along both of its axes (in the orientation's frame) is used, and the line's
// bounding block is centred in the box, the odd pixel going to the right and
// the bottom.

#include "images/treatments/caption_drawer.hpp"

#include "platform/error.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace pdfkit::images::treatments {

namespace {

struct BitmapGlyph {
  std::uint32_t height;
  char character;
  std::uint32_t width;
  std::uint32_t stride;
  std::uint32_t offset;
};

#include "images/bitmap_font_data.inc"

const BitmapGlyph* glyph(std::uint32_t height, char character) {
  for (const BitmapGlyph& entry : bitmap_font_glyphs) {
    if (entry.height == height && entry.character == character) {
      return &entry;
    }
  }
  return nullptr;
}

bool in_alphabet(char character) {
  return glyph(bitmap_font_heights[0], character) != nullptr;
}

}  // namespace

std::string CaptionDrawer::outside(std::string_view caption) {
  std::string out;
  for (const char character : caption) {
    if (!in_alphabet(character) && out.find(character) == std::string::npos) {
      out += character;
    }
  }
  return out;
}

std::uint32_t CaptionDrawer::width(std::string_view caption, std::uint32_t height) {
  std::uint32_t total = 0;
  for (const char character : caption) {
    const BitmapGlyph* entry = glyph(height, character);
    if (entry == nullptr) {
      return 0;
    }
    total += entry->width;
  }
  return total;
}

std::span<const std::uint32_t> CaptionDrawer::heights() noexcept {
  return bitmap_font_heights;
}

std::uint32_t CaptionDrawer::draw(PixelBuffer& buffer, const PixelBuffer::Rect& rect,
                                  std::string_view caption, std::span<const std::uint16_t> ink,
                                  const Orientation& orientation) {
  if (const std::string bad = outside(caption); !bad.empty()) {
    throw platform::Error(platform::ErrorCode::invalid_argument,
                          "the caption \"" + std::string(caption) + "\" holds the character" +
                              (bad.size() > 1 ? "s \"" : " \"") + bad +
                              "\", which the caption font does not have",
                          "a caption of the characters [, ], _, 0 to 9 and A to Z",
                          "write the caption in capital letters and digits only");
  }
  const PixelBuffer::Rect area = buffer.clip(rect);
  if (area.empty() || caption.empty()) {
    return 0;
  }
  const bool turned = orientation.right_x == 0;
  const std::uint32_t along = turned ? area.height() : area.width();
  const std::uint32_t across = turned ? area.width() : area.height();
  std::uint32_t height = 0;
  std::uint32_t line = 0;
  for (const std::uint32_t candidate : bitmap_font_heights) {
    const std::uint32_t needed = width(caption, candidate);
    if (candidate <= across && needed <= along) {
      height = candidate;
      line = needed;
    }
  }
  if (height == 0) {
    return 0;
  }
  // The block's extent from the caption origin along each pixel axis.
  const auto low = [](int step, std::uint32_t count) {
    return std::min(0, step * (static_cast<int>(count) - 1));
  };
  const int min_x = low(orientation.right_x, line) + low(orientation.down_x, height);
  const int min_y = low(orientation.right_y, line) + low(orientation.down_y, height);
  const int extent_x = static_cast<int>(turned ? height : line);
  const int extent_y = static_cast<int>(turned ? line : height);
  const int origin_x =
      static_cast<int>(area.left) + ((static_cast<int>(area.width()) - extent_x + 1) / 2) - min_x;
  const int origin_y =
      static_cast<int>(area.top) + ((static_cast<int>(area.height()) - extent_y + 1) / 2) - min_y;
  int pen = 0;
  for (const char character : caption) {
    // Every character was found at this height when the line was measured.
    const BitmapGlyph* found = glyph(height, character);
    if (found == nullptr) {
      continue;
    }
    const BitmapGlyph& entry = *found;
    for (std::uint32_t row = 0; row < entry.height; ++row) {
      for (std::uint32_t column = 0; column < entry.width; ++column) {
        const std::uint8_t byte =
            bitmap_font_rows[entry.offset + (row * entry.stride) + (column / 8)];
        if ((byte & (0x80U >> (column % 8))) == 0) {
          continue;
        }
        const int cx = pen + static_cast<int>(column);
        const int cy = static_cast<int>(row);
        const int x = origin_x + (cx * orientation.right_x) + (cy * orientation.down_x);
        const int y = origin_y + (cx * orientation.right_y) + (cy * orientation.down_y);
        if (std::cmp_greater_equal(x, area.left) && std::cmp_less(x, area.right) &&
            std::cmp_greater_equal(y, area.top) && std::cmp_less(y, area.bottom)) {
          buffer.set_pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), ink);
        }
      }
    }
    pen += static_cast<int>(entry.width);
  }
  return height;
}

std::string_view CaptionDrawer::recorded_sha256() noexcept {
  return bitmap_font_sha256;
}

std::vector<std::string> CaptionDrawer::canonical_lines() {
  constexpr std::string_view hex = "0123456789abcdef";
  std::vector<std::string> lines;
  for (const BitmapGlyph& entry : bitmap_font_glyphs) {
    std::string line = std::to_string(entry.height) + " " + entry.character + " " +
                       std::to_string(entry.width) + " ";
    for (std::uint32_t i = 0; i < entry.height * entry.stride; ++i) {
      const std::uint8_t byte = bitmap_font_rows[entry.offset + i];
      line += hex[byte >> 4U];
      line += hex[byte & 0x0FU];
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

}  // namespace pdfkit::images::treatments
