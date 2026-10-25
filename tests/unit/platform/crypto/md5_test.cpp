// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the MD5 digest against the published suite of RFC 1321 appendix A.5,
// against digests of messages that straddle the padding boundaries, and of the
// incremental interface against the one-shot one: the PDF key derivation feeds
// MD5 many small pieces, so the splitting must never change the digest.

#include "platform/crypto/md5.hpp"

#include "platform/byte_span.hpp"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

using pdfkit::platform::as_bytes;
using pdfkit::platform::ByteSpan;
using pdfkit::platform::crypto::Md5;

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

std::string md5_hex(std::string_view text) {
  const auto digest = Md5::digest(as_bytes(text));
  return hex(digest);
}

struct Vector {
  std::string_view message;
  std::string_view digest;
};

// RFC 1321 appendix A.5 "Test suite", read from https://www.rfc-editor.org/rfc/rfc1321.txt.
constexpr std::array<Vector, 7> rfc1321_suite{{
    {"", "d41d8cd98f00b204e9800998ecf8427e"},
    {"a", "0cc175b9c0f1b6a831c399e269772661"},
    {"abc", "900150983cd24fb0d6963f7d28e17f72"},
    {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
    {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
    {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
     "d174ab98d277d9f5a5611c2c9f419d9f"},
    {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
     "57edf4a22be3c955ac49da2e2107b67a"},
}};

TEST_CASE("md5 matches the seven vectors of RFC 1321 appendix A.5") {
  for (const Vector& v : rfc1321_suite) {
    INFO("message: " << v.message);
    CHECK(md5_hex(v.message) == v.digest);
  }
}

struct LengthVector {
  std::size_t length;
  std::string_view digest;
};

// MD5 of 'a' repeated length times, for lengths on both sides of the 56 and 64 byte padding
// boundaries. Computed with Python's hashlib.md5; the algorithm's own vectors are above.
constexpr std::array<LengthVector, 12> repeated_a{{
    {55, "ef1772b6dff9a122358552954ad0df65"},
    {56, "3b0c8ac703f828b04c6c197006d17218"},
    {57, "652b906d60af96844ebd21b674f35e93"},
    {63, "b06521f39153d618550606be297466d5"},
    {64, "014842d480b571495a4a0363793f7367"},
    {65, "c743a45e0d2e6a95cb859adae0248435"},
    {111, "089f243d1e831c5879aa375ee364a06e"},
    {112, "9146ef3527c7cfcc66dc615c3986e391"},
    {113, "d727cfdfc9ed0347e6917a68b982f7bc"},
    {127, "020406e1d05cdc2aa287641f7ae2cc39"},
    {128, "e510683b3f5ffe4093d021808bc6ff70"},
    {129, "b325dc1c6f5e7a2b7cf465b9feab7948"},
}};

TEST_CASE("md5 pads messages at the block boundaries") {
  for (const LengthVector& v : repeated_a) {
    INFO("length: " << v.length);
    const std::string message(v.length, 'a');
    CHECK(md5_hex(message) == v.digest);
  }
}

TEST_CASE("md5 gives the same digest however the message is split") {
  const std::string message(300, 'q');
  const std::string one_shot = md5_hex(message);
  const ByteSpan all = as_bytes(message);
  for (const std::size_t first : {0U, 1U, 55U, 56U, 63U, 64U, 65U, 127U, 128U, 129U, 299U, 300U}) {
    for (const std::size_t second : {0U, 1U, 7U, 64U, 100U}) {
      const std::size_t middle = std::min(first + second, all.size());
      Md5 md5;
      md5.update(all.first(first));
      md5.update(all.subspan(first, middle - first));
      md5.update(all.subspan(middle));
      INFO("split at " << first << " and " << middle);
      CHECK(hex(md5.finish()) == one_shot);
    }
  }
}

TEST_CASE("md5 fed one byte at a time equals the one-shot digest") {
  const std::string message = "message digest";
  Md5 md5;
  for (const char c : message) {
    const std::array<std::byte, 1> one{static_cast<std::byte>(c)};
    md5.update(one);
  }
  CHECK(hex(md5.finish()) == "f96b697d7cb7938d525a2f31aaf161d0");
}

TEST_CASE("md5 ignores an empty update") {
  Md5 md5;
  md5.update(ByteSpan{});
  md5.update(as_bytes("abc"));
  md5.update(ByteSpan{});
  CHECK(hex(md5.finish()) == "900150983cd24fb0d6963f7d28e17f72");
}

}  // namespace
