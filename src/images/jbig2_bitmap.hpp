// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Jbig2Bitmap is the bi-level bitmap every JBIG2 procedure works on: one byte
// per pixel, 1 for black, row by row, so that the context templates of the
// decoding procedures read neighbours without bit arithmetic (pdf.js holds its
// bitmaps the same way, as arrays of byte rows). A bitmap's memory is a claim
// made by the file (a width times a height), so a bitmap is created through an
// Allowance that checks the claim against the decoding budget and keeps the
// running total of one decode, because a symbol dictionary of many large
// symbols is as hostile as one large page. Drawing one bitmap on another is
// counted in the same Allowance, because a count of instances times the area of
// a large symbol is as much work as the file can ask for without a large file.

#ifndef PDFKIT_IMAGES_JBIG2_BITMAP_HPP
#define PDFKIT_IMAGES_JBIG2_BITMAP_HPP

#include "platform/budget.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::images {

class Jbig2Bitmap {
 public:
  // The bytes one decode may hold in bitmaps, against the budget's per-stream
  // limit.
  struct Allowance {
    platform::Budget* budget = nullptr;
    // Names the stream in a refusal, such as "object 12".
    std::string object;
    std::uint64_t reserved = 0;
    // The pixels drawn so far.
    std::uint64_t drawn = 0;

    // Counts pixels drawn; throws platform::Error (limit_exceeded) when the
    // total passes eight times the per-stream byte limit.
    void spend(std::uint64_t pixels);
  };

  // The combination operators of the standard (T.88 7.4.1.5): OR, AND, XOR,
  // XNOR and REPLACE.
  enum class Operator : std::uint8_t { bit_or, bit_and, bit_xor, bit_xnor, replace };

  Jbig2Bitmap() = default;

  // A bitmap of width by height pixels all set to fill (0 or 1). Throws
  // platform::Error (limit_exceeded) when the claim passes the budget; what
  // names the bitmap in the refusal, such as "a symbol bitmap".
  [[nodiscard]] static Jbig2Bitmap create(Allowance& allowance, std::uint32_t width,
                                          std::uint32_t height, std::uint8_t fill,
                                          std::string_view what);

  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

  // The pixel at (x, y), 0 outside the bitmap.
  [[nodiscard]] std::uint8_t get(std::int64_t x, std::int64_t y) const noexcept {
    if (x < 0 || y < 0 || std::cmp_greater_equal(x, width_) || std::cmp_greater_equal(y, height_)) {
      return 0;
    }
    return pixels_[static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)];
  }

  // Sets a pixel inside the bitmap.
  void set(std::uint32_t x, std::uint32_t y, std::uint8_t value) noexcept {
    pixels_[static_cast<std::size_t>(y) * width_ + x] = value;
  }

  [[nodiscard]] std::span<std::uint8_t> row(std::uint32_t y) noexcept {
    return {pixels_.data() + static_cast<std::size_t>(y) * width_, width_};
  }
  [[nodiscard]] std::span<const std::uint8_t> row(std::uint32_t y) const noexcept {
    return {pixels_.data() + static_cast<std::size_t>(y) * width_, width_};
  }

  // Combines source into this bitmap with its top left pixel at (x, y), under
  // op; pixels falling outside this bitmap are dropped. The pixels drawn are
  // spent from allowance.
  void combine(const Jbig2Bitmap& source, std::int64_t x, std::int64_t y, Operator op,
               Allowance& allowance);

  // The sub-rectangle from column first, count columns wide, over the full
  // height: how a collective bitmap is cut into symbols and patterns.
  [[nodiscard]] Jbig2Bitmap columns(Allowance& allowance, std::uint32_t first,
                                    std::uint32_t count) const;

 private:
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::vector<std::uint8_t> pixels_;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_JBIG2_BITMAP_HPP
