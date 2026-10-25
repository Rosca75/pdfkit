// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The MD5 algorithm of RFC 1321 (https://www.rfc-editor.org/rfc/rfc1321.txt,
// read): 64 steps over each 64-byte block in four rounds, a message padded with
// a 0x80 byte, zeros and its length in bits as a 64-bit little-endian number.
// The 64 additive constants and the rotation amounts are the ones the RFC lists
// in its section 3.4 and its reference code; the constants are the integer part
// of 2^32 times abs(sin(i)) for i = 1..64, and the table below was generated
// from that rule and compared with the RFC's listing.

#include "platform/crypto/md5.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pdfkit::platform::crypto {

namespace {

constexpr std::size_t block_bytes = 64;

// RFC 1321 section 3.4, the table T[1..64].
constexpr std::array<std::uint32_t, 64> t_table{
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU, 0xa8304613U,
    0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U,
    0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU,
    0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
    0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U,
    0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU,
    0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U,
    0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
    0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U, 0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU,
    0xeb86d391U,
};

// The rotation amounts of the four rounds (S11..S14, S21..S24, S31..S34, S41..S44 of the RFC).
constexpr std::array<int, 64> rotations{
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,  // round 1
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,  // round 2
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,  // round 3
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,  // round 4
};

std::uint32_t load_le32(std::span<const std::byte, block_bytes> block, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 4; i > 0; --i) {
    value = (value << 8U) | std::to_integer<std::uint32_t>(block[offset + i - 1]);
  }
  return value;
}

void transform(std::array<std::uint32_t, 4>& state, std::span<const std::byte, block_bytes> block) {
  std::array<std::uint32_t, 16> x{};
  for (std::size_t i = 0; i < x.size(); ++i) {
    x[i] = load_le32(block, i * 4);
  }
  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  for (std::size_t i = 0; i < 64; ++i) {
    std::uint32_t f = 0;
    std::size_t g = 0;
    switch (i / 16) {
      case 0:  // F(b, c, d) = (b and c) or (not b and d)
        f = (b & c) | (~b & d);
        g = i;
        break;
      case 1:  // G(b, c, d) = (b and d) or (c and not d)
        f = (b & d) | (c & ~d);
        g = (5 * i + 1) % 16;
        break;
      case 2:  // H(b, c, d) = b xor c xor d
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
        break;
      default:  // I(b, c, d) = c xor (b or not d)
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
        break;
    }
    const std::uint32_t sum = a + f + t_table[i] + x[g];
    a = d;
    d = c;
    c = b;
    b = b + std::rotl(sum, rotations[i]);
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

}  // namespace

// RFC 1321 section 3.3, the initial chaining value A, B, C, D.
Md5::Md5() : state_{0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U} {}

void Md5::update(ByteSpan data) {
  // The RFC counts the length in bits modulo 2^64; bytes modulo 2^64 is the same for every
  // message a process can hold.
  length_ += data.size();
  while (!data.empty()) {
    if (buffered_ == 0 && data.size() >= block_bytes) {
      transform(state_, data.first<block_bytes>());
      data = data.subspan(block_bytes);
      continue;
    }
    const std::size_t take = std::min(data.size(), block_bytes - buffered_);
    std::copy_n(data.begin(), take, block_.begin() + static_cast<std::ptrdiff_t>(buffered_));
    buffered_ += take;
    data = data.subspan(take);
    if (buffered_ == block_bytes) {
      transform(state_, block_);
      buffered_ = 0;
    }
  }
}

std::array<std::byte, 16> Md5::finish() {
  const std::uint64_t bits = length_ << 3U;
  block_[buffered_++] = std::byte{0x80};
  if (buffered_ > block_bytes - 8) {
    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(buffered_), block_.end(), std::byte{0});
    transform(state_, block_);
    buffered_ = 0;
  }
  std::fill(block_.begin() + static_cast<std::ptrdiff_t>(buffered_), block_.end() - 8,
            std::byte{0});
  for (std::size_t i = 0; i < 8; ++i) {
    block_[block_bytes - 8 + i] = static_cast<std::byte>((bits >> (8U * i)) & 0xffU);
  }
  transform(state_, block_);

  std::array<std::byte, 16> out{};
  for (std::size_t word = 0; word < 4; ++word) {
    for (std::size_t i = 0; i < 4; ++i) {
      out[word * 4 + i] = static_cast<std::byte>((state_[word] >> (8U * i)) & 0xffU);
    }
  }
  return out;
}

std::array<std::byte, 16> Md5::digest(ByteSpan data) {
  Md5 md5;
  md5.update(data);
  return md5.finish();
}

}  // namespace pdfkit::platform::crypto
