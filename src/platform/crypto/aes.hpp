// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Aes is the AES block cipher of FIPS 197 with 128- and 256-bit keys, the two
// sizes the PDF standard security handler uses (revisions 4 to 6; ISO 32000-2
// section 7.6.3, recalled), plus the CBC
// chaining mode of NIST SP 800-38A and PKCS#7 padding. CBC here works on whole
// 16-byte blocks and adds no padding, because PDF stores the IV in front of the
// ciphertext and the caller decides what a bad final block means. The code is
// a plain byte-oriented implementation: a reader of documents has no secret
// worth protecting from timing, and the tables stay checkable against FIPS 197.

#ifndef PDFKIT_PLATFORM_CRYPTO_AES_HPP
#define PDFKIT_PLATFORM_CRYPTO_AES_HPP

#include "platform/byte_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pdfkit::platform::crypto {

class Aes {
 public:
  static constexpr std::size_t block_size = 16;

  // The result of pkcs7_unpad: the bytes with a valid pad removed, or all the
  // bytes and padding_valid false.
  struct Unpadded {
    std::vector<std::byte> bytes;
    bool padding_valid;
  };

  // key is 16 or 32 bytes; any other length is an invalid_argument error.
  explicit Aes(ByteSpan key);

  // in and out may be the same block.
  void encrypt_block(std::span<const std::byte, 16> in, std::span<std::byte, 16> out) const;
  void decrypt_block(std::span<const std::byte, 16> in, std::span<std::byte, 16> out) const;

  // CBC decryption of the whole 16-byte blocks of data, without unpadding; a
  // trailing partial block is dropped. iv16 must be 16 bytes (invalid_argument
  // otherwise), as must the key be 16 or 32.
  [[nodiscard]] static std::vector<std::byte> cbc_decrypt(ByteSpan key, ByteSpan iv16,
                                                          ByteSpan data);

  // CBC encryption without padding; data must be a multiple of 16 bytes
  // (invalid_argument otherwise).
  [[nodiscard]] static std::vector<std::byte> cbc_encrypt(ByteSpan key, ByteSpan iv16,
                                                          ByteSpan data);

  // data followed by n bytes of value n, n in 1..16, so the result is a
  // multiple of 16 bytes.
  [[nodiscard]] static std::vector<std::byte> pkcs7_pad(ByteSpan data);

  // Removes the PKCS#7 pad when the last byte n is 1..16 and the last n bytes
  // all equal n; otherwise keeps every byte and reports padding_valid false,
  // because the PDF reader tolerates a bad pad.
  [[nodiscard]] static Unpadded pkcs7_unpad(ByteSpan data);

 private:
  std::array<std::uint8_t, 240> round_keys_{};  // up to 15 round keys of 16 bytes
  std::size_t rounds_;
};

}  // namespace pdfkit::platform::crypto

#endif  // PDFKIT_PLATFORM_CRYPTO_AES_HPP
