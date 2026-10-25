// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Jbig2Bitmap's allocation under the decoding budget and the combination of
// two bitmaps under the operators of ITU-T T.88 section 7.4.1.5 (recalled; the
// operator numbers are those of the standard's region segment flags).

#include "images/jbig2_bitmap.hpp"

#include "platform/checked.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace pdfkit::images {

Jbig2Bitmap Jbig2Bitmap::create(Allowance& allowance, std::uint32_t width, std::uint32_t height,
                                std::uint8_t fill, std::string_view what) {
  const std::string claim = std::string(what) + " of " + allowance.object;
  const auto pixels = platform::checked_mul<std::uint64_t>(width, height, claim);
  const auto total = platform::checked_add<std::uint64_t>(allowance.reserved, pixels, claim);
  allowance.budget->check_claim(total, claim);
  allowance.reserved = total;
  Jbig2Bitmap bitmap;
  bitmap.width_ = width;
  bitmap.height_ = height;
  bitmap.pixels_.assign(static_cast<std::size_t>(pixels), fill);
  return bitmap;
}

void Jbig2Bitmap::combine(const Jbig2Bitmap& source, std::int64_t x, std::int64_t y, Operator op) {
  const std::int64_t first_row = std::max<std::int64_t>(0, -y);
  const std::int64_t last_row =
      std::min<std::int64_t>(source.height_, static_cast<std::int64_t>(height_) - y);
  const std::int64_t first_column = std::max<std::int64_t>(0, -x);
  const std::int64_t last_column =
      std::min<std::int64_t>(source.width_, static_cast<std::int64_t>(width_) - x);
  for (std::int64_t sy = first_row; sy < last_row; ++sy) {
    const auto source_row = source.row(static_cast<std::uint32_t>(sy));
    auto target_row = row(static_cast<std::uint32_t>(y + sy));
    for (std::int64_t sx = first_column; sx < last_column; ++sx) {
      const std::uint8_t s = source_row[static_cast<std::size_t>(sx)];
      std::uint8_t& t = target_row[static_cast<std::size_t>(x + sx)];
      switch (op) {
        case Operator::bit_or:
          t = static_cast<std::uint8_t>(t | s);
          break;
        case Operator::bit_and:
          t = static_cast<std::uint8_t>(t & s);
          break;
        case Operator::bit_xor:
          t = static_cast<std::uint8_t>(t ^ s);
          break;
        case Operator::bit_xnor:
          t = static_cast<std::uint8_t>(1U ^ (static_cast<unsigned>(t) ^ static_cast<unsigned>(s)));
          break;
        case Operator::replace:
          t = s;
          break;
      }
    }
  }
}

Jbig2Bitmap Jbig2Bitmap::columns(Allowance& allowance, std::uint32_t first,
                                 std::uint32_t count) const {
  Jbig2Bitmap out = create(allowance, count, height_, 0, "a bitmap cut from a collective bitmap");
  for (std::uint32_t y = 0; y < height_; ++y) {
    const auto from = row(y);
    auto to = out.row(y);
    for (std::uint32_t x = 0; x < count; ++x) {
      const std::uint64_t column = static_cast<std::uint64_t>(first) + x;
      if (column < width_) {
        to[x] = from[static_cast<std::size_t>(column)];
      }
    }
  }
  return out;
}

}  // namespace pdfkit::images
