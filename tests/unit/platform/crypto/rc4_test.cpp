// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of RC4 against the keystream vectors of RFC 6229, the short
// plaintext/ciphertext pairs every RC4 description quotes, the in-place and
// one-shot interfaces agreeing, and the refusal of a key outside 1 to 256 bytes.

#include "platform/crypto/rc4.hpp"

#include "platform/byte_span.hpp"
#include "platform/error.hpp"

#include <doctest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using pdfkit::platform::as_bytes;
using pdfkit::platform::ByteSpan;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::crypto::Rc4;

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

struct Keystream {
  std::string_view key;
  std::size_t offset;
  std::string_view bytes;  // the 16 keystream bytes at offset
};

// RFC 6229 section 2, https://www.rfc-editor.org/rfc/rfc6229.txt (read): the keystream bytes at
// offsets 0, 16, 240, 256, 496 and 512 for the keys of 40, 56, 64, 80, 128, 192 and 256 bits that
// start 01 02 03 04 05 ...; the keystream of the key is what RC4 XORs into the data.
constexpr std::array<Keystream, 42> rfc6229{{
    // key 0102030405
    {"0102030405", 0, "b2396305f03dc027ccc3524a0a1118a8"},
    {"0102030405", 16, "6982944f18fc82d589c403a47a0d0919"},
    {"0102030405", 240, "28cb1132c96ce286421dcaadb8b69eae"},
    {"0102030405", 256, "1cfcf62b03eddb641d77dfcf7f8d8c93"},
    {"0102030405", 496, "42b7d0cdd918a8a33dd51781c81f4041"},
    {"0102030405", 512, "6459844432a7da923cfb3eb4980661f6"},
    // key 01020304050607
    {"01020304050607", 0, "293f02d47f37c9b633f2af5285feb46b"},
    {"01020304050607", 16, "e620f1390d19bd84e2e0fd752031afc1"},
    {"01020304050607", 240, "914f02531c9218810df60f67e338154c"},
    {"01020304050607", 256, "d0fdb583073ce85ab83917740ec011d5"},
    {"01020304050607", 496, "75f81411e871cffa70b90c74c592e454"},
    {"01020304050607", 512, "0bb87202938dad609e87a5a1b079e5e4"},
    // key 0102030405060708
    {"0102030405060708", 0, "97ab8a1bf0afb96132f2f67258da15a8"},
    {"0102030405060708", 16, "8263efdb45c4a18684ef87e6b19e5b09"},
    {"0102030405060708", 240, "9636ebc9841926f4f7d1f362bddf6e18"},
    {"0102030405060708", 256, "d0a990ff2c05fef5b90373c9ff4b870a"},
    {"0102030405060708", 496, "73239f1db7f41d80b643c0c52518ec63"},
    {"0102030405060708", 512, "163b319923a6bdb4527c626126703c0f"},
    // key 0102030405060708090a
    {"0102030405060708090a", 0, "ede3b04643e586cc907dc21851709902"},
    {"0102030405060708090a", 16, "03516ba78f413beb223aa5d4d2df6711"},
    {"0102030405060708090a", 240, "3cfd6cb58ee0fdde640176ad0000044d"},
    {"0102030405060708090a", 256, "48532b21fb6079c9114c0ffd9c04a1ad"},
    {"0102030405060708090a", 496, "3e8cea98017109979084b1ef92f99d86"},
    {"0102030405060708090a", 512, "e20fb49bdb337ee48b8d8dc0f4afeffe"},
    // key 0102030405060708090a0b0c0d0e0f10
    {"0102030405060708090a0b0c0d0e0f10", 0, "9ac7cc9a609d1ef7b2932899cde41b97"},
    {"0102030405060708090a0b0c0d0e0f10", 16, "5248c4959014126a6e8a84f11d1a9e1c"},
    {"0102030405060708090a0b0c0d0e0f10", 240, "065902e4b620f6cc36c8589f66432f2b"},
    {"0102030405060708090a0b0c0d0e0f10", 256, "d39d566bc6bce3010768151549f3873f"},
    {"0102030405060708090a0b0c0d0e0f10", 496, "b6d1e6c4a5e4771cad79538df295fb11"},
    {"0102030405060708090a0b0c0d0e0f10", 512, "c68c1d5c559a974123df1dbc52a43b89"},
    // key 0102030405060708090a0b0c0d0e0f101112131415161718
    {"0102030405060708090a0b0c0d0e0f101112131415161718", 0, "0595e57fe5f0bb3c706edac8a4b2db11"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718", 16, "dfde31344a1af769c74f070aee9e2326"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718", 240, "b06b9b1e195d13d8f4a7995c4553ac05"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718", 256, "6bd2378ec341c9a42f37ba79f88a32ff"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718", 496, "e70bce1df7645adb5d2c4130215c3522"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718", 512, "9a5730c7fcb4c9af51ffda89c7f1ad22"},
    // key 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20
    {"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", 0,
     "eaa6bd25880bf93d3f5d1e4ca2611d91"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", 16,
     "cfa45c9f7e714b54bdfa80027cb14380"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", 240,
     "114ae344ded71b35f2e60febad727fd8"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", 256,
     "02e1e7056b0f623900496422943e97b6"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", 496,
     "91cb93c787964e10d9527d999c6f936b"},
    {"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", 512,
     "49b18b42f8e8367cbeb5ef104ba1c7cd"},
}};

TEST_CASE("rc4 reproduces the keystreams of RFC 6229") {
  for (const Keystream& v : rfc6229) {
    const auto key = unhex(v.key);
    std::vector<std::byte> stream(v.offset + 16, std::byte{0});
    Rc4 rc4(key);
    rc4.apply(stream);
    INFO("key " << v.key << " offset " << v.offset);
    CHECK(hex(ByteSpan(stream).subspan(v.offset)) == v.bytes);
  }
}

TEST_CASE("rc4 encrypts the plaintexts every RC4 description quotes") {
  // The pairs "Key"/"Plaintext", "Wiki"/"pedia" and "Secret"/"Attack at dawn" are the examples
  // of https://en.wikipedia.org/wiki/RC4 (recalled), confirmed with an independent
  // implementation of the cipher written for the check.
  const auto encrypt = [](std::string_view key, std::string_view plain) {
    return hex(Rc4::transform(as_bytes(key), as_bytes(plain)));
  };
  CHECK(encrypt("Key", "Plaintext") == "bbf316e8d940af0ad3");
  CHECK(encrypt("Wiki", "pedia") == "1021bf0420");
  CHECK(encrypt("Secret", "Attack at dawn") == "45a01f645fc35b383552544b9bf5");
}

TEST_CASE("rc4 decrypts what it encrypts") {
  const auto key = unhex("0102030405060708090a0b0c0d0e0f10");
  const std::string text(1000, 'x');
  const auto cipher = Rc4::transform(key, as_bytes(text));
  CHECK(cipher.size() == text.size());
  const ByteSpan plain = as_bytes(text);
  CHECK(Rc4::transform(key, cipher) == std::vector<std::byte>(plain.begin(), plain.end()));
}

TEST_CASE("rc4 continues its keystream across apply calls") {
  const auto key = unhex("0102030405");
  const std::string text(700, 'z');
  const auto whole = Rc4::transform(key, as_bytes(text));

  const ByteSpan plain = as_bytes(text);
  std::vector<std::byte> pieces(plain.begin(), plain.end());
  Rc4 rc4(key);
  const std::span<std::byte> all(pieces);
  rc4.apply(all.first(1));
  rc4.apply(all.subspan(1, 255));
  rc4.apply(std::span<std::byte>{});
  rc4.apply(all.subspan(256));
  CHECK(pieces == whole);
}

TEST_CASE("rc4 of no data is no data") {
  const auto key = unhex("01");
  CHECK(Rc4::transform(key, ByteSpan{}).empty());
}

TEST_CASE("rc4 takes keys of 1 to 256 bytes and refuses any other length") {
  for (const std::size_t size : {1U, 5U, 16U, 255U, 256U}) {
    const std::vector<std::byte> key(size, std::byte{7});
    INFO("key size " << size);
    CHECK(Rc4::transform(key, as_bytes("data")).size() == 4);
  }
  for (const std::size_t size : {0U, 257U, 1000U}) {
    const std::vector<std::byte> key(size, std::byte{7});
    INFO("key size " << size);
    try {
      (void)Rc4::transform(key, as_bytes("data"));
      FAIL("a key of this size must be refused");
    } catch (const Error& error) {
      CHECK(error.code() == ErrorCode::invalid_argument);
      CHECK_FALSE(error.failed().empty());
      CHECK_FALSE(error.expected().empty());
      CHECK_FALSE(error.fix().empty());
    }
  }
}

}  // namespace
