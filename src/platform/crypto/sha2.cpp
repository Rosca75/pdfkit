// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The SHA-256, SHA-384 and SHA-512 algorithms of FIPS 180-4 (NIST, "Secure Hash
// Standard", https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf, read).
// SHA-256 works on 32-bit words in 64-byte blocks, SHA-512 on 64-bit words in
// 128-byte blocks, and SHA-384 is SHA-512 with other initial values and a
// digest cut to 48 bytes. The constants are the fractional parts of the square
// and cube roots of the first primes (FIPS 180-4 sections 4.2 and 5.3); the
// tables below were generated from that rule and compared with the standard's
// listings.

#include "platform/crypto/sha2.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pdfkit::platform::crypto {

namespace {

constexpr std::size_t block_bytes_256 = 64;
constexpr std::size_t block_bytes_512 = 128;

// FIPS 180-4 section 4.2.2, the constants K0..K63 of SHA-256.
constexpr std::array<std::uint32_t, 64> k256{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U,
};

// FIPS 180-4 section 4.2.3, the constants K0..K79 of SHA-384 and SHA-512.
constexpr std::array<std::uint64_t, 80> k512{
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

// FIPS 180-4 section 5.3.3, the initial hash value of SHA-256.
constexpr std::array<std::uint32_t, 8> h256{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
};

// FIPS 180-4 section 5.3.4, the initial hash value of SHA-384.
constexpr std::array<std::uint64_t, 8> h384{
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};

// FIPS 180-4 section 5.3.5, the initial hash value of SHA-512.
constexpr std::array<std::uint64_t, 8> h512{
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

template <typename Word>
Word load_be(std::span<const std::byte> block, std::size_t offset) {
  Word value = 0;
  for (std::size_t i = 0; i < sizeof(Word); ++i) {
    value = static_cast<Word>((value << 8U) | std::to_integer<Word>(block[offset + i]));
  }
  return value;
}

template <typename Word>
void store_be(Word value, std::span<std::byte> out, std::size_t offset) {
  for (std::size_t i = 0; i < sizeof(Word); ++i) {
    const std::size_t shift = 8U * (sizeof(Word) - 1 - i);
    out[offset + i] = static_cast<std::byte>((value >> shift) & 0xffU);
  }
}

// FIPS 180-4 section 4.1.2, the functions of SHA-256.
std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
  return (x & y) ^ (~x & z);
}
std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}
std::uint32_t big_sigma0(std::uint32_t x) {
  return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
}
std::uint32_t big_sigma1(std::uint32_t x) {
  return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
}
std::uint32_t small_sigma0(std::uint32_t x) {
  return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3U);
}
std::uint32_t small_sigma1(std::uint32_t x) {
  return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10U);
}

// FIPS 180-4 section 4.1.3, the functions of SHA-384 and SHA-512.
std::uint64_t ch(std::uint64_t x, std::uint64_t y, std::uint64_t z) {
  return (x & y) ^ (~x & z);
}
std::uint64_t maj(std::uint64_t x, std::uint64_t y, std::uint64_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}
std::uint64_t big_sigma0(std::uint64_t x) {
  return std::rotr(x, 28) ^ std::rotr(x, 34) ^ std::rotr(x, 39);
}
std::uint64_t big_sigma1(std::uint64_t x) {
  return std::rotr(x, 14) ^ std::rotr(x, 18) ^ std::rotr(x, 41);
}
std::uint64_t small_sigma0(std::uint64_t x) {
  return std::rotr(x, 1) ^ std::rotr(x, 8) ^ (x >> 7U);
}
std::uint64_t small_sigma1(std::uint64_t x) {
  return std::rotr(x, 19) ^ std::rotr(x, 61) ^ (x >> 6U);
}

// FIPS 180-4 section 6.2.2, one block of SHA-256.
void compress256(std::array<std::uint32_t, 8>& state, std::span<const std::byte> block) {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t t = 0; t < 16; ++t) {
    w[t] = load_be<std::uint32_t>(block, t * 4);
  }
  for (std::size_t t = 16; t < 64; ++t) {
    w[t] = small_sigma1(w[t - 2]) + w[t - 7] + small_sigma0(w[t - 15]) + w[t - 16];
  }
  std::array<std::uint32_t, 8> v = state;  // a, b, c, d, e, f, g, h
  for (std::size_t t = 0; t < 64; ++t) {
    const std::uint32_t t1 = v[7] + big_sigma1(v[4]) + ch(v[4], v[5], v[6]) + k256[t] + w[t];
    const std::uint32_t t2 = big_sigma0(v[0]) + maj(v[0], v[1], v[2]);
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
    state[i] += v[i];
  }
}

// FIPS 180-4 section 6.4.2, one block of SHA-384 or SHA-512.
void compress512(std::array<std::uint64_t, 8>& state, std::span<const std::byte> block) {
  std::array<std::uint64_t, 80> w{};
  for (std::size_t t = 0; t < 16; ++t) {
    w[t] = load_be<std::uint64_t>(block, t * 8);
  }
  for (std::size_t t = 16; t < 80; ++t) {
    w[t] = small_sigma1(w[t - 2]) + w[t - 7] + small_sigma0(w[t - 15]) + w[t - 16];
  }
  std::array<std::uint64_t, 8> v = state;  // a, b, c, d, e, f, g, h
  for (std::size_t t = 0; t < 80; ++t) {
    const std::uint64_t t1 = v[7] + big_sigma1(v[4]) + ch(v[4], v[5], v[6]) + k512[t] + w[t];
    const std::uint64_t t2 = big_sigma0(v[0]) + maj(v[0], v[1], v[2]);
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
    state[i] += v[i];
  }
}

}  // namespace

Sha2::Sha2(Variant variant) : variant_(variant) {
  switch (variant_) {
    case Variant::sha256:
      state32_ = h256;
      break;
    case Variant::sha384:
      state64_ = h384;
      break;
    case Variant::sha512:
      state64_ = h512;
      break;
  }
}

std::size_t Sha2::block_size() const noexcept {
  return variant_ == Variant::sha256 ? block_bytes_256 : block_bytes_512;
}

void Sha2::compress() {
  const std::span<const std::byte> block(block_.data(), block_size());
  if (variant_ == Variant::sha256) {
    compress256(state32_, block);
  } else {
    compress512(state64_, block);
  }
}

void Sha2::update(ByteSpan data) {
  // The standard counts the length in bits (modulo 2^64 for SHA-256, 2^128 for the others);
  // a byte count in 64 bits is exact for every message a process can hold.
  length_ += data.size();
  const std::size_t size = block_size();
  while (!data.empty()) {
    const std::size_t take = std::min(data.size(), size - buffered_);
    std::copy_n(data.begin(), take, block_.begin() + static_cast<std::ptrdiff_t>(buffered_));
    buffered_ += take;
    data = data.subspan(take);
    if (buffered_ == size) {
      compress();
      buffered_ = 0;
    }
  }
}

std::vector<std::byte> Sha2::finish() {
  // FIPS 180-4 section 5.1: a one bit, zeros, then the length in bits as a 64-bit (SHA-256) or
  // 128-bit (SHA-384, SHA-512) big-endian number, filling the last block.
  const std::size_t size = block_size();
  const std::size_t length_field = variant_ == Variant::sha256 ? 8 : 16;
  const std::uint64_t bits_low = length_ << 3U;
  const std::uint64_t bits_high = length_ >> 61U;
  block_[buffered_++] = std::byte{0x80};
  if (buffered_ > size - length_field) {
    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(buffered_),
              block_.begin() + static_cast<std::ptrdiff_t>(size), std::byte{0});
    compress();
    buffered_ = 0;
  }
  std::fill(block_.begin() + static_cast<std::ptrdiff_t>(buffered_),
            block_.begin() + static_cast<std::ptrdiff_t>(size), std::byte{0});
  store_be<std::uint64_t>(bits_low, block_, size - 8);
  if (variant_ != Variant::sha256) {
    store_be<std::uint64_t>(bits_high, block_, size - 16);
  }
  compress();

  std::vector<std::byte> out;
  switch (variant_) {
    case Variant::sha256:
      out.resize(32);
      for (std::size_t i = 0; i < 8; ++i) {
        store_be<std::uint32_t>(state32_[i], out, i * 4);
      }
      break;
    case Variant::sha384:
      out.resize(48);
      for (std::size_t i = 0; i < 6; ++i) {
        store_be<std::uint64_t>(state64_[i], out, i * 8);
      }
      break;
    case Variant::sha512:
      out.resize(64);
      for (std::size_t i = 0; i < 8; ++i) {
        store_be<std::uint64_t>(state64_[i], out, i * 8);
      }
      break;
  }
  return out;
}

std::vector<std::byte> Sha2::digest(Variant variant, ByteSpan data) {
  Sha2 sha(variant);
  sha.update(data);
  return sha.finish();
}

}  // namespace pdfkit::platform::crypto
