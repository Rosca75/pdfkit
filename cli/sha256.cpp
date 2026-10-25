// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// SHA-256 of FIPS 180-4 sections 4.1.2, 4.2.2, 5.1.1, 5.3.3 and 6.2 (recalled):
// the round constants, the initial hash value, the padding and the
// compression function, written for one-shot use over a buffer in memory.

#include "sha256.hpp"

#include <array>
#include <bit>

namespace pdfkit::cli {

namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

void compress(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t i = 0; i < 16; ++i) {
    w.at(i) = (static_cast<std::uint32_t>(block[4 * i]) << 24U) |
              (static_cast<std::uint32_t>(block[4 * i + 1]) << 16U) |
              (static_cast<std::uint32_t>(block[4 * i + 2]) << 8U) |
              static_cast<std::uint32_t>(block[4 * i + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 =
        std::rotr(w.at(i - 15), 7) ^ std::rotr(w.at(i - 15), 18) ^ (w.at(i - 15) >> 3U);
    const std::uint32_t s1 =
        std::rotr(w.at(i - 2), 17) ^ std::rotr(w.at(i - 2), 19) ^ (w.at(i - 2) >> 10U);
    w.at(i) = w.at(i - 16) + s0 + w.at(i - 7) + s1;
  }
  std::array<std::uint32_t, 8> v = state;
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25);
    const std::uint32_t choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
    const std::uint32_t t1 = v[7] + s1 + choose + round_constants.at(i) + w.at(i);
    const std::uint32_t s0 = std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22);
    const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    const std::uint32_t t2 = s0 + majority;
    v[7] = v[6];
    v[6] = v[5];
    v[5] = v[4];
    v[4] = v[3] + t1;
    v[3] = v[2];
    v[2] = v[1];
    v[1] = v[0];
    v[0] = t1 + t2;
  }
  for (std::size_t i = 0; i < 8; ++i) {
    state.at(i) += v.at(i);
  }
}

}  // namespace

std::string Sha256::hex(const std::uint8_t* data, std::size_t size) {
  std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                     0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::size_t at = 0;
  for (; size - at >= 64; at += 64) {
    compress(state, data + at);
  }
  // The last block: the remaining bytes, 0x80, zeros, and the bit length.
  std::array<std::uint8_t, 128> tail{};
  const std::size_t rest = size - at;
  for (std::size_t i = 0; i < rest; ++i) {
    tail.at(i) = data[at + i];
  }
  tail.at(rest) = 0x80;
  const std::size_t blocks = rest + 9 > 64 ? 2 : 1;
  const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8U;
  for (std::size_t i = 0; i < 8; ++i) {
    tail.at(blocks * 64 - 1 - i) = static_cast<std::uint8_t>((bits >> (8U * i)) & 0xFFU);
  }
  for (std::size_t block = 0; block < blocks; ++block) {
    compress(state, tail.data() + block * 64);
  }
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (const std::uint32_t word : state) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      out.push_back(digits[(word >> static_cast<unsigned>(shift)) & 0x0FU]);
    }
  }
  return out;
}

}  // namespace pdfkit::cli
