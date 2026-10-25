// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Sha2 is the SHA-256, SHA-384 and SHA-512 hash family of FIPS 180-4, written
// here because the PDF standard security handler of revisions 5 and 6 (AES-256
// documents) builds its key checks from these three hashes (ISO 32000-2 section
// 7.6.4.3, recalled) and the standard library carries none of them. The object hashes a message fed
// in any number of pieces; digest() hashes one piece.

#ifndef PDFKIT_PLATFORM_CRYPTO_SHA2_HPP
#define PDFKIT_PLATFORM_CRYPTO_SHA2_HPP

#include "platform/byte_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pdfkit::platform::crypto {

class Sha2 {
 public:
  enum class Variant : std::uint8_t { sha256, sha384, sha512 };

  explicit Sha2(Variant variant);

  // Feeds more of the message; the pieces concatenate.
  void update(ByteSpan data);

  // Pads the message and returns its digest: 32, 48 or 64 bytes by variant. The
  // object is spent afterwards: construct another Sha2 for the next message.
  [[nodiscard]] std::vector<std::byte> finish();

  // The digest of data in one call.
  [[nodiscard]] static std::vector<std::byte> digest(Variant variant, ByteSpan data);

 private:
  [[nodiscard]] std::size_t block_size() const noexcept;
  void compress();

  Variant variant_;
  std::array<std::uint32_t, 8> state32_{};  // sha256
  std::array<std::uint64_t, 8> state64_{};  // sha384 and sha512
  std::array<std::byte, 128> block_{};
  std::size_t buffered_ = 0;
  std::uint64_t length_ = 0;  // message bytes fed so far
};

}  // namespace pdfkit::platform::crypto

#endif  // PDFKIT_PLATFORM_CRYPTO_SHA2_HPP
