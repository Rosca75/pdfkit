// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Md5 is the MD5 message digest of RFC 1321, written here because the PDF
// standard security handler derives its file key and its owner and user values
// from MD5 (revisions 2 to 4; ISO 32000-1 section 7.6.3.3, recalled) and no
// standard library carries it. It is a checksum for that format, never a
// protection choice of ours. The object hashes a message fed in any number of
// pieces; digest() hashes one piece.

#ifndef PDFKIT_PLATFORM_CRYPTO_MD5_HPP
#define PDFKIT_PLATFORM_CRYPTO_MD5_HPP

#include "platform/byte_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pdfkit::platform::crypto {

class Md5 {
 public:
  Md5();

  // Feeds more of the message; the pieces concatenate.
  void update(ByteSpan data);

  // Pads the message and returns its digest. The object is spent afterwards:
  // construct another Md5 for the next message.
  [[nodiscard]] std::array<std::byte, 16> finish();

  // The digest of data in one call.
  [[nodiscard]] static std::array<std::byte, 16> digest(ByteSpan data);

 private:
  std::array<std::uint32_t, 4> state_;
  std::array<std::byte, 64> block_{};
  std::size_t buffered_ = 0;
  std::uint64_t length_ = 0;  // message bytes fed so far
};

}  // namespace pdfkit::platform::crypto

#endif  // PDFKIT_PLATFORM_CRYPTO_MD5_HPP
