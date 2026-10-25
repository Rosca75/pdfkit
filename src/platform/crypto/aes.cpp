// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The AES cipher of FIPS 197 (NIST, "Advanced Encryption Standard",
// https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197.pdf, read): the key
// expansion of section 5.2, the cipher of section 5.1 and the straightforward
// inverse cipher of section 5.3, over a state held as 16 bytes in input order
// (column c, row r at index 4c + r). The S-box, its inverse and the round
// constants are the tables of section 5.1.1, 5.3.2 and 5.2; they were generated
// from the field definition and compared with the standard's listings. CBC is
// NIST SP 800-38A section 6.2 (https://nvlpubs.nist.gov/nistpubs/Legacy/SP/
// nistspecialpublication800-38a.pdf, read), PKCS#7 padding is RFC 5652
// section 6.3 (https://www.rfc-editor.org/rfc/rfc5652.txt, recalled).

#include "platform/crypto/aes.hpp"

#include "platform/error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pdfkit::platform::crypto {

namespace {

using Block = std::array<std::uint8_t, 16>;

// FIPS 197 figure 7, the S-box.
constexpr std::array<std::uint8_t, 256> sbox{
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

// FIPS 197 figure 14, the inverse S-box.
constexpr std::array<std::uint8_t, 256> inverse_sbox{
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

// FIPS 197 section 5.2, Rcon[1..10]: the powers of x in GF(2^8), stored in the top byte of a word
// there and here as that byte alone.
constexpr std::array<std::uint8_t, 10> rcon{
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
};

[[noreturn]] void throw_bad_key(std::size_t size) {
  throw Error(ErrorCode::invalid_argument,
              "an AES key of " + std::to_string(size) + " bytes was given",
              "a key of 16 bytes (AES-128) or 32 bytes (AES-256)",
              "pass a key of one of those lengths; a document whose security handler asks for "
              "another key length cannot be decrypted by this library");
}

[[noreturn]] void throw_bad_iv(std::size_t size) {
  throw Error(ErrorCode::invalid_argument,
              "an AES-CBC initialisation vector of " + std::to_string(size) + " bytes was given",
              "an initialisation vector of exactly 16 bytes",
              "pass the 16 bytes that precede the ciphertext; a shorter encrypted string or "
              "stream is damaged");
}

std::size_t rounds_for(ByteSpan key) {
  if (key.size() == 16) {
    return 10;
  }
  if (key.size() == 32) {
    return 14;
  }
  throw_bad_key(key.size());
}

// Multiplication by x in GF(2^8) modulo x^8 + x^4 + x^3 + x + 1 (FIPS 197 section 4.2.1). The
// field arithmetic runs on unsigned values of at most 8 bits.
constexpr unsigned xtime(unsigned value) {
  return ((value << 1U) ^ ((value & 0x80U) != 0 ? 0x1bU : 0U)) & 0xffU;
}

// Multiplication in GF(2^8) (FIPS 197 section 4.2).
constexpr unsigned multiply(unsigned a, unsigned b) {
  unsigned product = 0;
  for (unsigned bit = 0; bit < 8; ++bit) {
    if (((b >> bit) & 1U) != 0) {
      product ^= a;
    }
    a = xtime(a);
  }
  return product;
}

Block load_block(std::span<const std::byte, 16> in) {
  Block block{};
  for (std::size_t i = 0; i < 16; ++i) {
    block[i] = std::to_integer<std::uint8_t>(in[i]);
  }
  return block;
}

void store_block(const Block& block, std::span<std::byte, 16> out) {
  for (std::size_t i = 0; i < 16; ++i) {
    out[i] = static_cast<std::byte>(block[i]);
  }
}

using RoundKeys = std::array<std::uint8_t, 240>;

void add_round_key(Block& state, const RoundKeys& keys, std::size_t round) {
  for (std::size_t i = 0; i < 16; ++i) {
    state[i] ^= keys[round * 16 + i];
  }
}

void sub_bytes(Block& state) {
  for (std::uint8_t& byte : state) {
    byte = sbox[byte];
  }
}

void inverse_sub_bytes(Block& state) {
  for (std::uint8_t& byte : state) {
    byte = inverse_sbox[byte];
  }
}

// Row r of the state moves left by r columns (FIPS 197 section 5.1.2).
void shift_rows(Block& state) {
  const Block old = state;
  for (std::size_t column = 0; column < 4; ++column) {
    for (std::size_t row = 0; row < 4; ++row) {
      state[4 * column + row] = old[4 * ((column + row) % 4) + row];
    }
  }
}

void inverse_shift_rows(Block& state) {
  const Block old = state;
  for (std::size_t column = 0; column < 4; ++column) {
    for (std::size_t row = 0; row < 4; ++row) {
      state[4 * ((column + row) % 4) + row] = old[4 * column + row];
    }
  }
}

// Each column is multiplied by the fixed polynomial {03}x^3 + {01}x^2 + {01}x + {02}
// (FIPS 197 section 5.1.3).
void mix_columns(Block& state) {
  for (std::size_t column = 0; column < 4; ++column) {
    const unsigned a0 = state[4 * column];
    const unsigned a1 = state[4 * column + 1];
    const unsigned a2 = state[4 * column + 2];
    const unsigned a3 = state[4 * column + 3];
    state[4 * column] = static_cast<std::uint8_t>(xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3);
    state[4 * column + 1] = static_cast<std::uint8_t>(a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3);
    state[4 * column + 2] = static_cast<std::uint8_t>(a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3);
    state[4 * column + 3] = static_cast<std::uint8_t>(xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3));
  }
}

// The inverse polynomial {0b}x^3 + {0d}x^2 + {09}x + {0e} (FIPS 197 section 5.3.3).
void inverse_mix_columns(Block& state) {
  for (std::size_t column = 0; column < 4; ++column) {
    const unsigned a0 = state[4 * column];
    const unsigned a1 = state[4 * column + 1];
    const unsigned a2 = state[4 * column + 2];
    const unsigned a3 = state[4 * column + 3];
    state[4 * column] = static_cast<std::uint8_t>(multiply(a0, 0x0eU) ^ multiply(a1, 0x0bU) ^
                                                  multiply(a2, 0x0dU) ^ multiply(a3, 0x09U));
    state[4 * column + 1] = static_cast<std::uint8_t>(multiply(a0, 0x09U) ^ multiply(a1, 0x0eU) ^
                                                      multiply(a2, 0x0bU) ^ multiply(a3, 0x0dU));
    state[4 * column + 2] = static_cast<std::uint8_t>(multiply(a0, 0x0dU) ^ multiply(a1, 0x09U) ^
                                                      multiply(a2, 0x0eU) ^ multiply(a3, 0x0bU));
    state[4 * column + 3] = static_cast<std::uint8_t>(multiply(a0, 0x0bU) ^ multiply(a1, 0x0dU) ^
                                                      multiply(a2, 0x09U) ^ multiply(a3, 0x0eU));
  }
}

// FIPS 197 section 5.2, KeyExpansion: round_keys holds the Nb * (Nr + 1) words as bytes.
void expand_key(ByteSpan key, RoundKeys& round_keys, std::size_t rounds) {
  const std::size_t nk = key.size() / 4;
  const std::size_t words = 4 * (rounds + 1);
  for (std::size_t i = 0; i < key.size(); ++i) {
    round_keys[i] = std::to_integer<std::uint8_t>(key[i]);
  }
  for (std::size_t i = nk; i < words; ++i) {
    std::array<std::uint8_t, 4> temp{};
    for (std::size_t j = 0; j < 4; ++j) {
      temp[j] = round_keys[4 * (i - 1) + j];
    }
    if (i % nk == 0) {
      // RotWord, SubWord, then the round constant into the first byte.
      const std::uint8_t first = temp[0];
      temp[0] = static_cast<std::uint8_t>(sbox[temp[1]] ^ rcon[i / nk - 1]);
      temp[1] = sbox[temp[2]];
      temp[2] = sbox[temp[3]];
      temp[3] = sbox[first];
    } else if (nk > 6 && i % nk == 4) {
      for (std::uint8_t& byte : temp) {
        byte = sbox[byte];
      }
    }
    for (std::size_t j = 0; j < 4; ++j) {
      round_keys[4 * i + j] = static_cast<std::uint8_t>(round_keys[4 * (i - nk) + j] ^ temp[j]);
    }
  }
}

}  // namespace

Aes::Aes(ByteSpan key) : rounds_(rounds_for(key)) {
  expand_key(key, round_keys_, rounds_);
}

void Aes::encrypt_block(std::span<const std::byte, 16> in, std::span<std::byte, 16> out) const {
  Block state = load_block(in);
  add_round_key(state, round_keys_, 0);
  for (std::size_t round = 1; round < rounds_; ++round) {
    sub_bytes(state);
    shift_rows(state);
    mix_columns(state);
    add_round_key(state, round_keys_, round);
  }
  sub_bytes(state);
  shift_rows(state);
  add_round_key(state, round_keys_, rounds_);
  store_block(state, out);
}

void Aes::decrypt_block(std::span<const std::byte, 16> in, std::span<std::byte, 16> out) const {
  Block state = load_block(in);
  add_round_key(state, round_keys_, rounds_);
  for (std::size_t round = rounds_ - 1; round >= 1; --round) {
    inverse_shift_rows(state);
    inverse_sub_bytes(state);
    add_round_key(state, round_keys_, round);
    inverse_mix_columns(state);
  }
  inverse_shift_rows(state);
  inverse_sub_bytes(state);
  add_round_key(state, round_keys_, 0);
  store_block(state, out);
}

std::vector<std::byte> Aes::cbc_decrypt(ByteSpan key, ByteSpan iv16, ByteSpan data) {
  const Aes aes(key);
  if (iv16.size() != block_size) {
    throw_bad_iv(iv16.size());
  }
  const std::size_t blocks = data.size() / block_size;
  std::vector<std::byte> out(blocks * block_size);
  std::array<std::byte, 16> previous{};
  std::ranges::copy(iv16, previous.begin());
  for (std::size_t i = 0; i < blocks; ++i) {
    const auto in = data.subspan(i * block_size, block_size);
    const std::span<std::byte, 16> plain =
        std::span<std::byte>(out).subspan(i * block_size).first<16>();
    aes.decrypt_block(in.first<16>(), plain);
    for (std::size_t j = 0; j < block_size; ++j) {
      plain[j] ^= previous[j];
    }
    std::ranges::copy(in, previous.begin());
  }
  return out;
}

std::vector<std::byte> Aes::cbc_encrypt(ByteSpan key, ByteSpan iv16, ByteSpan data) {
  const Aes aes(key);
  if (iv16.size() != block_size) {
    throw_bad_iv(iv16.size());
  }
  if (data.size() % block_size != 0) {
    throw Error(ErrorCode::invalid_argument,
                "AES-CBC encryption was given " + std::to_string(data.size()) +
                    " bytes, which is not a whole number of 16-byte blocks",
                "data whose length is a multiple of 16 bytes",
                "pad the data first (Aes::pkcs7_pad) so it fills whole blocks");
  }
  const std::size_t blocks = data.size() / block_size;
  std::vector<std::byte> out(data.size());
  std::array<std::byte, 16> previous{};
  std::ranges::copy(iv16, previous.begin());
  for (std::size_t i = 0; i < blocks; ++i) {
    std::array<std::byte, 16> mixed{};
    for (std::size_t j = 0; j < block_size; ++j) {
      mixed[j] = data[i * block_size + j] ^ previous[j];
    }
    const std::span<std::byte, 16> cipher =
        std::span<std::byte>(out).subspan(i * block_size).first<16>();
    aes.encrypt_block(mixed, cipher);
    std::ranges::copy(cipher, previous.begin());
  }
  return out;
}

std::vector<std::byte> Aes::pkcs7_pad(ByteSpan data) {
  const std::size_t pad = block_size - (data.size() % block_size);
  std::vector<std::byte> out(data.begin(), data.end());
  out.resize(data.size() + pad, static_cast<std::byte>(pad));
  return out;
}

Aes::Unpadded Aes::pkcs7_unpad(ByteSpan data) {
  Unpadded result{std::vector<std::byte>(data.begin(), data.end()), false};
  if (data.empty()) {
    return result;
  }
  const auto pad = std::to_integer<std::size_t>(data.back());
  if (pad < 1 || pad > block_size || pad > data.size()) {
    return result;
  }
  const bool all_equal =
      std::ranges::all_of(data.last(pad), [&](std::byte b) { return b == data.back(); });
  if (all_equal) {
    result.bytes.resize(data.size() - pad);
    result.padding_valid = true;
  }
  return result;
}

}  // namespace pdfkit::platform::crypto
