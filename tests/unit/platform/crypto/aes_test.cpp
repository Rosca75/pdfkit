// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of AES against the published vectors: the block examples of FIPS 197
// appendix C (AES-128 and AES-256) and the four-block CBC examples of NIST
// SP 800-38A appendix F.2, plus round trips, the refusals of a bad key, IV or
// length, and PKCS#7 padding including the bad pad the PDF reader tolerates.

#include "platform/crypto/aes.hpp"

#include "platform/byte_span.hpp"
#include "platform/error.hpp"

#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using pdfkit::platform::ByteSpan;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::crypto::Aes;

std::string hex(ByteSpan bytes) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (const std::byte b : bytes) {
    const auto v = std::to_integer<unsigned>(b);
    out.push_back(digits[v >> 4U]);
    out.push_back(digits[v & 15U]);
  }
  return out;
}

std::vector<std::byte> unhex(std::string_view text) {
  const auto nibble = [](char c) -> unsigned {
    return c >= 'a' ? static_cast<unsigned>(c - 'a') + 10U : static_cast<unsigned>(c - '0');
  };
  std::vector<std::byte> out;
  for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
    out.push_back(static_cast<std::byte>((nibble(text[i]) << 4U) | nibble(text[i + 1])));
  }
  return out;
}

template <typename Fn>
ErrorCode code_of(Fn&& fn) {
  try {
    fn();
  } catch (const Error& error) {
    return error.code();
  }
  return ErrorCode::internal;
}

std::vector<std::byte> bytes_of(std::size_t count, std::uint8_t first) {
  std::vector<std::byte> out(count);
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = static_cast<std::byte>(static_cast<std::uint8_t>(first + i * 7U));
  }
  return out;
}

// FIPS 197 appendix C, https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197.pdf (read): the
// plaintext is shared by C.1 (AES-128) and C.3 (AES-256).
constexpr std::string_view fips197_plain = "00112233445566778899aabbccddeeff";
constexpr std::string_view fips197_key128 = "000102030405060708090a0b0c0d0e0f";
constexpr std::string_view fips197_cipher128 = "69c4e0d86a7b0430d8cdb78070b4c55a";
constexpr std::string_view fips197_key256 =
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
constexpr std::string_view fips197_cipher256 = "8ea2b7ca516745bfeafc49904b496089";

std::string encrypt_one(std::string_view key_hex, std::string_view plain_hex) {
  const auto key = unhex(key_hex);
  const auto plain = unhex(plain_hex);
  const Aes aes(key);
  std::array<std::byte, 16> out{};
  aes.encrypt_block(std::span<const std::byte, 16>(plain), out);
  return hex(out);
}

std::string decrypt_one(std::string_view key_hex, std::string_view cipher_hex) {
  const auto key = unhex(key_hex);
  const auto cipher = unhex(cipher_hex);
  const Aes aes(key);
  std::array<std::byte, 16> out{};
  aes.decrypt_block(std::span<const std::byte, 16>(cipher), out);
  return hex(out);
}

TEST_CASE("aes encrypts and decrypts the FIPS 197 appendix C.1 block with a 128-bit key") {
  CHECK(encrypt_one(fips197_key128, fips197_plain) == fips197_cipher128);
  CHECK(decrypt_one(fips197_key128, fips197_cipher128) == fips197_plain);
}

TEST_CASE("aes encrypts and decrypts the FIPS 197 appendix C.3 block with a 256-bit key") {
  CHECK(encrypt_one(fips197_key256, fips197_plain) == fips197_cipher256);
  CHECK(decrypt_one(fips197_key256, fips197_cipher256) == fips197_plain);
}

TEST_CASE("aes transforms a block in place when input and output are the same") {
  const auto key = unhex(fips197_key128);
  const Aes aes(key);
  auto block = unhex(fips197_plain);
  const std::span<std::byte, 16> view(block);
  aes.encrypt_block(view, view);
  CHECK(hex(block) == fips197_cipher128);
  aes.decrypt_block(view, view);
  CHECK(hex(block) == fips197_plain);
}

// NIST SP 800-38A appendix F.2, https://nvlpubs.nist.gov/nistpubs/Legacy/SP/
// nistspecialpublication800-38a.pdf (read): the plaintext, the IV and the ciphertext of
// F.2.1 (CBC-AES128.Encrypt) and F.2.5 (CBC-AES256.Encrypt); F.2.2 and F.2.6 decrypt the same.
constexpr std::string_view sp800_iv = "000102030405060708090a0b0c0d0e0f";
constexpr std::string_view sp800_plain =
    "6bc1bee22e409f96e93d7e117393172a"
    "ae2d8a571e03ac9c9eb76fac45af8e51"
    "30c81c46a35ce411e5fbc1191a0a52ef"
    "f69f2445df4f9b17ad2b417be66c3710";
constexpr std::string_view sp800_key128 = "2b7e151628aed2a6abf7158809cf4f3c";
constexpr std::string_view sp800_cipher128 =
    "7649abac8119b246cee98e9b12e9197d"
    "5086cb9b507219ee95db113a917678b2"
    "73bed6b8e3c1743b7116e69e22229516"
    "3ff1caa1681fac09120eca307586e1a7";
constexpr std::string_view sp800_key256 =
    "603deb1015ca71be2b73aef0857d7781"
    "1f352c073b6108d72d9810a30914dff4";
constexpr std::string_view sp800_cipher256 =
    "f58c4c04d6e5f1ba779eabfb5f7bfbd6"
    "9cfc4e967edb808d679f777bc6702c7d"
    "39f23369a9d9bacfa530e26304231461"
    "b2eb05e2c39be9fcda6c19078c6a9d1b";

TEST_CASE("aes cbc matches the SP 800-38A F.2.1 and F.2.2 vectors with a 128-bit key") {
  const auto key = unhex(sp800_key128);
  const auto iv = unhex(sp800_iv);
  CHECK(hex(Aes::cbc_encrypt(key, iv, unhex(sp800_plain))) == sp800_cipher128);
  CHECK(hex(Aes::cbc_decrypt(key, iv, unhex(sp800_cipher128))) == sp800_plain);
}

TEST_CASE("aes cbc matches the SP 800-38A F.2.5 and F.2.6 vectors with a 256-bit key") {
  const auto key = unhex(sp800_key256);
  const auto iv = unhex(sp800_iv);
  CHECK(hex(Aes::cbc_encrypt(key, iv, unhex(sp800_plain))) == sp800_cipher256);
  CHECK(hex(Aes::cbc_decrypt(key, iv, unhex(sp800_cipher256))) == sp800_plain);
}

TEST_CASE("aes cbc of nothing is nothing") {
  const auto key = unhex(sp800_key128);
  const auto iv = unhex(sp800_iv);
  CHECK(Aes::cbc_encrypt(key, iv, ByteSpan{}).empty());
  CHECK(Aes::cbc_decrypt(key, iv, ByteSpan{}).empty());
}

TEST_CASE("aes cbc decryption drops a trailing partial block") {
  const auto key = unhex(sp800_key128);
  const auto iv = unhex(sp800_iv);
  auto cipher = unhex(sp800_cipher128);
  const std::size_t whole = cipher.size();
  for (const std::size_t extra : {1U, 8U, 15U}) {
    cipher.resize(whole);
    cipher.resize(whole + extra, std::byte{0xaa});
    CHECK(hex(Aes::cbc_decrypt(key, iv, cipher)) == sp800_plain);
  }
  cipher.resize(10);
  CHECK(Aes::cbc_decrypt(key, iv, cipher).empty());
}

TEST_CASE("aes cbc round trips every length that is a multiple of 16 under both key sizes") {
  const auto iv = bytes_of(16, 3);
  for (const std::size_t key_size : {16U, 32U}) {
    const auto key = bytes_of(key_size, 9);
    for (std::size_t blocks = 0; blocks <= 9; ++blocks) {
      const auto plain = bytes_of(blocks * 16, 41);
      const auto cipher = Aes::cbc_encrypt(key, iv, plain);
      CHECK(cipher.size() == plain.size());
      CHECK(Aes::cbc_decrypt(key, iv, cipher) == plain);
    }
  }
}

TEST_CASE("aes refuses a key that is not 16 or 32 bytes") {
  for (const std::size_t size : {0U, 1U, 15U, 17U, 24U, 31U, 33U, 64U}) {
    const auto key = bytes_of(size, 1);
    INFO("key size " << size);
    CHECK(code_of([&] { const Aes aes(key); }) == ErrorCode::invalid_argument);
  }
  const auto good = bytes_of(16, 1);
  const auto iv = bytes_of(16, 2);
  const auto short_key = bytes_of(24, 1);
  CHECK(code_of([&] { (void)Aes::cbc_decrypt(short_key, iv, good); }) ==
        ErrorCode::invalid_argument);
  CHECK(code_of([&] { (void)Aes::cbc_encrypt(short_key, iv, good); }) ==
        ErrorCode::invalid_argument);
}

TEST_CASE("aes cbc refuses an IV that is not 16 bytes") {
  const auto key = bytes_of(16, 1);
  const auto data = bytes_of(32, 5);
  for (const std::size_t size : {0U, 8U, 15U, 17U, 32U}) {
    const auto iv = bytes_of(size, 2);
    INFO("iv size " << size);
    CHECK(code_of([&] { (void)Aes::cbc_decrypt(key, iv, data); }) == ErrorCode::invalid_argument);
    CHECK(code_of([&] { (void)Aes::cbc_encrypt(key, iv, data); }) == ErrorCode::invalid_argument);
  }
}

TEST_CASE("aes cbc encryption refuses data that is not a multiple of 16 bytes") {
  const auto key = bytes_of(16, 1);
  const auto iv = bytes_of(16, 2);
  for (const std::size_t size : {1U, 15U, 17U, 31U}) {
    const auto data = bytes_of(size, 5);
    INFO("data size " << size);
    CHECK(code_of([&] { (void)Aes::cbc_encrypt(key, iv, data); }) == ErrorCode::invalid_argument);
  }
}

TEST_CASE("aes refusals carry the three message parts") {
  try {
    const auto key = bytes_of(24, 1);
    const Aes aes(key);
    FAIL("a 24-byte key must be refused");
  } catch (const Error& error) {
    CHECK_FALSE(error.failed().empty());
    CHECK_FALSE(error.expected().empty());
    CHECK_FALSE(error.fix().empty());
  }
}

TEST_CASE("pkcs7 padding fills to the next multiple of 16 and adds a block to a full one") {
  for (std::size_t size = 0; size <= 40; ++size) {
    const auto data = bytes_of(size, 11);
    const auto padded = Aes::pkcs7_pad(data);
    const std::size_t pad = 16 - (size % 16);
    INFO("size " << size);
    REQUIRE(padded.size() == size + pad);
    CHECK(padded.size() % 16 == 0);
    for (std::size_t i = 0; i < size; ++i) {
      CHECK(padded[i] == data[i]);
    }
    for (std::size_t i = size; i < padded.size(); ++i) {
      CHECK(std::to_integer<std::size_t>(padded[i]) == pad);
    }
  }
  CHECK(hex(Aes::pkcs7_pad(ByteSpan{})) == "10101010101010101010101010101010");
  CHECK(hex(Aes::pkcs7_pad(unhex("00112233445566778899aabbccddee"))) ==
        "00112233445566778899aabbccddee01");
}

TEST_CASE("pkcs7 unpadding removes a valid pad and reports it") {
  for (std::size_t size = 0; size <= 40; ++size) {
    const auto data = bytes_of(size, 11);
    const auto result = Aes::pkcs7_unpad(Aes::pkcs7_pad(data));
    INFO("size " << size);
    CHECK(result.padding_valid);
    CHECK(result.bytes == data);
  }
  const auto full = Aes::pkcs7_unpad(unhex("10101010101010101010101010101010"));
  CHECK(full.padding_valid);
  CHECK(full.bytes.empty());
}

TEST_CASE("pkcs7 unpadding keeps every byte when the pad is bad") {
  const std::vector<std::string_view> bad{
      "",                                  // nothing to unpad
      "00112233445566778899aabbccddeeff",  // last byte 0
      "00112233445566778899aabbccddee11",  // last byte 17, above the block size
      "00112233445566778899aabbccddee03",  // three bytes claimed, the earlier two differ
      "00112233445566778899aabbccdd0203",  // the byte before the last is 02, not 03
      "ff",                                // one byte claiming a pad longer than the data
      "0202020202020202020202020202ee02",  // a pad longer than the run of equal bytes
      "1010101010101010101010101010ff10",  // a pad of 16 whose run of 0x10 is broken
  };
  for (const std::string_view text : bad) {
    const auto data = unhex(text);
    const auto result = Aes::pkcs7_unpad(data);
    INFO("data " << text);
    CHECK_FALSE(result.padding_valid);
    CHECK(result.bytes == data);
  }
}

TEST_CASE("pkcs7 unpadding accepts the largest and smallest pads") {
  const auto one = Aes::pkcs7_unpad(unhex("aabbccddeeff00112233445566778801"));
  CHECK(one.padding_valid);
  CHECK(hex(one.bytes) == "aabbccddeeff001122334455667788");
  const auto sixteen = Aes::pkcs7_unpad(unhex("ab10101010101010101010101010101010"));
  CHECK(sixteen.padding_valid);
  CHECK(hex(sixteen.bytes) == "ab");
}

}  // namespace
