// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The row reader of SampleUnpacker. A /Decode array of the wrong length is
// replaced by [0 1] per component, as viewers ignore it.

#include "images/sample_unpacker.hpp"

#include <algorithm>
#include <utility>

namespace pdfkit::images {

SampleUnpacker::SampleUnpacker(std::uint32_t bits, std::size_t samples_per_row,
                               std::size_t components, std::vector<double> decode)
    : bits_(bits),
      samples_(samples_per_row),
      row_bytes_((samples_per_row * bits + 7) / 8),
      max_value_(bits >= 16 ? 65535U : (1U << bits) - 1U),
      decode_(std::move(decode)) {
  if (decode_.size() < 2 * components) {
    decode_.assign(2 * components, 0.0);
    for (std::size_t i = 0; i < components; ++i) {
      decode_[2 * i + 1] = 1.0;
    }
  }
  for (std::size_t i = 0; i < components; ++i) {
    if (decode_[2 * i] != 0.0 || decode_[2 * i + 1] != 1.0) {
      identity_ = false;
    }
  }
}

void SampleUnpacker::unpack(platform::ByteSpan row, std::span<std::uint16_t> out) const {
  const std::size_t count = std::min(samples_, out.size());
  const auto byte = [&](std::size_t index) -> unsigned {
    return index < row.size() ? std::to_integer<unsigned>(row[index]) : 0U;
  };
  switch (bits_) {
    case 8:
      for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<std::uint16_t>(byte(i));
      }
      return;
    case 16:
      for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<std::uint16_t>((byte(2 * i) << 8U) | byte(2 * i + 1));
      }
      return;
    default: {
      const unsigned per_byte = 8U / bits_;
      const unsigned mask = (1U << bits_) - 1U;
      for (std::size_t i = 0; i < count; ++i) {
        const unsigned shift = 8U - bits_ * static_cast<unsigned>(i % per_byte + 1);
        out[i] = static_cast<std::uint16_t>((byte(i / per_byte) >> shift) & mask);
      }
      return;
    }
  }
}

}  // namespace pdfkit::images
