// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Rc4 is the RC4 stream cipher, written here because no standard library
// carries it and the PDF standard security handler, revisions 2 to 4, encrypts
// strings and streams with it (ISO 32000-1 section 7.6.2, recalled). It exists to read those
// documents; nothing recommends it for new data. The same keystream encrypts and decrypts, so one
// apply() serves both directions.

#ifndef PDFKIT_PLATFORM_CRYPTO_RC4_HPP
#define PDFKIT_PLATFORM_CRYPTO_RC4_HPP

#include "platform/byte_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pdfkit::platform::crypto {

class Rc4 {
 public:
  // key is 1 to 256 bytes; any other length is an invalid_argument error.
  explicit Rc4(ByteSpan key);

  // XORs the next bytes of the keystream into data, in place.
  void apply(std::span<std::byte> data);

  // A copy of data XORed with the keystream of key from its first byte.
  [[nodiscard]] static std::vector<std::byte> transform(ByteSpan key, ByteSpan data);

 private:
  std::array<std::uint8_t, 256> state_{};
  std::uint8_t i_ = 0;
  std::uint8_t j_ = 0;
};

}  // namespace pdfkit::platform::crypto

#endif  // PDFKIT_PLATFORM_CRYPTO_RC4_HPP
