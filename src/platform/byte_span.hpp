// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ByteSpan, the read-only view of bytes every layer passes around, and the
// bounds-checked readers of big- and little-endian unsigned integers. A read
// past the end yields no value instead of a byte from elsewhere, and the caller
// decides whether that is damage or the end of the data.

#ifndef PDFKIT_PLATFORM_BYTE_SPAN_HPP
#define PDFKIT_PLATFORM_BYTE_SPAN_HPP

#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace pdfkit::platform {

using ByteSpan = std::span<const std::byte>;

// The bytes of a string, without copying.
[[nodiscard]] inline ByteSpan as_bytes(std::string_view text) noexcept {
  return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

// The unsigned integer of sizeof(T) bytes at offset, most significant first.
template <std::unsigned_integral T>
[[nodiscard]] std::optional<T> read_be(ByteSpan bytes, std::size_t offset) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
    return std::nullopt;
  }
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value = static_cast<T>((static_cast<unsigned long long>(value) << 8U) |
                           std::to_integer<unsigned>(bytes[offset + i]));
  }
  return value;
}

// The unsigned integer of sizeof(T) bytes at offset, least significant first.
template <std::unsigned_integral T>
[[nodiscard]] std::optional<T> read_le(ByteSpan bytes, std::size_t offset) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
    return std::nullopt;
  }
  T value = 0;
  for (std::size_t i = sizeof(T); i > 0; --i) {
    value = static_cast<T>((static_cast<unsigned long long>(value) << 8U) |
                           std::to_integer<unsigned>(bytes[offset + i - 1]));
  }
  return value;
}

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_BYTE_SPAN_HPP
