// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// SampleUnpacker reads an image's rows of samples, ISO 32000-1 section
// 8.9.5.2 (recalled), docs/DESIGN.md section 9.4: 1, 2, 4, 8 or 16 bits per
// component, most significant bit first, each row padded to a byte boundary,
// and maps a raw value through the image's /Decode array (two numbers per
// component; [1 0] inverts). A row shorter than its stride reads its missing
// samples as zero, as viewers read a short image.

#ifndef PDFKIT_IMAGES_SAMPLE_UNPACKER_HPP
#define PDFKIT_IMAGES_SAMPLE_UNPACKER_HPP

#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pdfkit::images {

class SampleUnpacker {
 public:
  // bits is 1, 2, 4, 8 or 16; samples_per_row is the width times the
  // components. decode holds two numbers per component (components of them).
  SampleUnpacker(std::uint32_t bits, std::size_t samples_per_row, std::size_t components,
                 std::vector<double> decode);

  [[nodiscard]] std::uint32_t bits() const noexcept { return bits_; }
  // The bytes of one padded row.
  [[nodiscard]] std::size_t row_bytes() const noexcept { return row_bytes_; }
  // The largest raw value, 2^bits - 1.
  [[nodiscard]] std::uint32_t max_value() const noexcept { return max_value_; }

  // The raw values of one row; row may be short.
  void unpack(platform::ByteSpan row, std::span<std::uint16_t> out) const;

  // A raw value of component through /Decode.
  [[nodiscard]] double decoded(std::uint32_t raw, std::size_t component) const noexcept {
    const std::size_t i = component * 2;
    return decode_[i] + static_cast<double>(raw) * (decode_[i + 1] - decode_[i]) / max_value_;
  }

  // Whether /Decode is [0 1] for every component.
  [[nodiscard]] bool identity_decode() const noexcept { return identity_; }

 private:
  std::uint32_t bits_;
  std::size_t samples_;
  std::size_t row_bytes_;
  std::uint32_t max_value_;
  std::vector<double> decode_;
  bool identity_ = true;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_SAMPLE_UNPACKER_HPP
