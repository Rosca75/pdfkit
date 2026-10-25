// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The test tree's own copy of the standard security handler's writing side.
// The algorithm numbers are those of ISO 32000-1 section 7.6.3 and ISO 32000-2
// section 7.6.4 (read in the design record's sources). Salts and IVs are
// derived from fixed values so a fixture is the same bytes on every run.

#include "support/pdf_encryptor.hpp"

#include "platform/byte_span.hpp"
#include "platform/crypto/aes.hpp"
#include "platform/crypto/md5.hpp"
#include "platform/crypto/rc4.hpp"
#include "platform/crypto/sha2.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pdfkit::test {

namespace {

using platform::crypto::Aes;
using platform::crypto::Md5;
using platform::crypto::Rc4;
using platform::crypto::Sha2;

// The padding string of ISO 32000-1 section 7.6.3.3, algorithm 2 step a (read).
constexpr std::array<unsigned char, 32> padding_bytes{
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};

std::string padding() {
  return {padding_bytes.begin(), padding_bytes.end()};
}

platform::ByteSpan span(const std::string& text) {
  return platform::as_bytes(text);
}

template <typename Bytes>
std::string text(const Bytes& bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (const std::byte value : bytes) {
    out.push_back(static_cast<char>(value));
  }
  return out;
}

std::string md5(const std::string& data) {
  return text(Md5::digest(span(data)));
}

std::string rc4(const std::string& key, const std::string& data) {
  return text(Rc4::transform(span(key), span(data)));
}

std::string padded(const std::string& password) {
  return (password + padding()).substr(0, 32);
}

std::string le32(std::int32_t value) {
  const auto bits = static_cast<std::uint32_t>(value);
  std::string out;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<char>((bits >> shift) & 0xFFU));
  }
  return out;
}

std::string aes_cbc(const std::string& key, const std::string& iv, const std::string& data) {
  return text(Aes::cbc_encrypt(span(key), span(iv), span(data)));
}

// Algorithm 2.B of ISO 32000-2 for revision 6; plain SHA-256 for revision 5.
std::string hash_r6(int revision, const std::string& password, const std::string& salt,
                    const std::string& user_key) {
  std::string k = text(Sha2::digest(Sha2::Variant::sha256, span(password + salt + user_key)));
  if (revision == 5) {
    return k;
  }
  std::string e;
  for (unsigned round = 0;; ++round) {
    std::string k1;
    std::string block = password;
    block += k;
    block += user_key;
    for (int i = 0; i < 64; ++i) {
      k1 += block;
    }
    e = aes_cbc(k.substr(0, 16), k.substr(16, 16), k1);
    unsigned sum = 0;
    for (std::size_t i = 0; i < 16; ++i) {
      sum += static_cast<unsigned char>(e[i]);
    }
    const Sha2::Variant variant = sum % 3 == 0   ? Sha2::Variant::sha256
                                  : sum % 3 == 1 ? Sha2::Variant::sha384
                                                 : Sha2::Variant::sha512;
    k = text(Sha2::digest(variant, span(e)));
    const unsigned last = static_cast<unsigned char>(e.back());
    if (round + 1 >= 64 && last <= round + 1 - 32) {
      break;
    }
  }
  return k.substr(0, 32);
}

std::string hex(const std::string& bytes) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out = "<";
  for (const char value : bytes) {
    const auto byte = static_cast<unsigned char>(value);
    out.push_back(digits[byte >> 4U]);
    out.push_back(digits[byte & 0x0FU]);
  }
  return out + ">";
}

}  // namespace

PdfEncryptor::PdfEncryptor(Spec spec, std::string id0)
    : spec_(std::move(spec)), id0_(std::move(id0)) {
  if (spec_.revision < 2 || spec_.revision > 6) {
    throw std::invalid_argument("PdfEncryptor: revision must be 2 to 6");
  }
  if (spec_.revision >= 5) {
    compute_r5_r6();
  } else {
    compute_r2_to_r4();
  }
}

void PdfEncryptor::compute_r2_to_r4() {
  const int revision = spec_.revision;
  const std::size_t n = revision == 2 ? 5U : static_cast<std::size_t>(spec_.key_bits / 8);
  // Algorithm 3: /O from the owner password.
  std::string owner_hash =
      md5(padded(spec_.owner_password.empty() ? spec_.user_password : spec_.owner_password));
  if (revision >= 3) {
    for (int i = 0; i < 50; ++i) {
      owner_hash = md5(owner_hash);
    }
  }
  const std::string owner_key = owner_hash.substr(0, n);
  o_ = rc4(owner_key, padded(spec_.user_password));
  if (revision >= 3) {
    for (unsigned i = 1; i <= 19; ++i) {
      std::string key = owner_key;
      for (char& value : key) {
        value = static_cast<char>(static_cast<unsigned char>(value) ^ i);
      }
      o_ = rc4(key, o_);
    }
  }
  // Algorithm 2: the file key.
  std::string input = padded(spec_.user_password) + o_ + le32(spec_.permissions) + id0_;
  if (revision >= 4 && !spec_.encrypt_metadata) {
    input += std::string(4, '\xFF');
  }
  std::string key = md5(input);
  if (revision >= 3) {
    for (int i = 0; i < 50; ++i) {
      key = md5(key.substr(0, n));
    }
  }
  file_key_ = key.substr(0, n);
  // Algorithms 4 and 5: /U.
  if (revision == 2) {
    u_ = rc4(file_key_, padding());
  } else {
    std::string value = rc4(file_key_, md5(padding() + id0_));
    for (unsigned i = 1; i <= 19; ++i) {
      std::string round_key = file_key_;
      for (char& byte : round_key) {
        byte = static_cast<char>(static_cast<unsigned char>(byte) ^ i);
      }
      value = rc4(round_key, value);
    }
    u_ = value + std::string(16, '\0');
  }
}

void PdfEncryptor::compute_r5_r6() {
  const int revision = spec_.revision;
  file_key_.clear();
  for (unsigned i = 0; i < 32; ++i) {
    file_key_.push_back(static_cast<char>((i * 37U + 11U) & 0xFFU));
  }
  const std::string user = spec_.user_password.substr(0, 127);
  const std::string owner = spec_.owner_password.substr(0, 127);
  const std::string user_validation = "uvsalt01";
  const std::string user_key_salt = "uksalt02";
  const std::string owner_validation = "ovsalt03";
  const std::string owner_key_salt = "oksalt04";
  const std::string zero_iv(16, '\0');
  // Algorithm 8: /U and /UE.
  u_ = hash_r6(revision, user, user_validation, "") + user_validation + user_key_salt;
  ue_ = aes_cbc(hash_r6(revision, user, user_key_salt, ""), zero_iv, file_key_);
  // Algorithm 9: /O and /OE.
  o_ = hash_r6(revision, owner, owner_validation, u_) + owner_validation + owner_key_salt;
  oe_ = aes_cbc(hash_r6(revision, owner, owner_key_salt, u_), zero_iv, file_key_);
  // Algorithm 10: /Perms.
  std::string perms = le32(spec_.permissions) + std::string(4, '\xFF');
  perms += spec_.encrypt_metadata ? 'T' : 'F';
  perms += spec_.corrupt_perms ? "xyz" : "adb";
  perms += "0123";
  perms_ = aes_cbc(file_key_, zero_iv, perms);
}

std::string PdfEncryptor::dictionary() const {
  const int revision = spec_.revision;
  std::string out = "<< /Filter /Standard";
  if (revision == 2) {
    out += " /V 1 /R 2 /Length 40";
  } else if (revision == 3) {
    out += " /V 2 /R 3 /Length " + std::to_string(spec_.key_bits);
  } else if (revision == 4) {
    const bool aes = spec_.method == Method::aes;
    out += " /V 4 /R 4 /Length 128 /CF << /StdCF << /CFM ";
    out += aes ? "/AESV2" : "/V2";
    out += " /AuthEvent /DocOpen /Length 16 >> >> /StmF /" + spec_.stream_filter + " /StrF /" +
           spec_.string_filter;
  } else {
    out += " /V 5 /R " + std::to_string(revision) +
           " /Length 256 /CF << /StdCF << /CFM /AESV3 /AuthEvent /DocOpen /Length 32 >> >>"
           " /StmF /StdCF /StrF /StdCF";
    out += " /OE " + hex(oe_) + " /UE " + hex(ue_) + " /Perms " + hex(perms_);
  }
  if (revision >= 4 && !spec_.encrypt_metadata) {
    out += " /EncryptMetadata false";
  }
  out += " /O " + hex(o_) + " /U " + hex(u_) + " /P " + std::to_string(spec_.permissions) + " >>";
  return out;
}

std::string PdfEncryptor::encrypt(std::uint32_t number, std::uint32_t generation,
                                  const std::string& bytes, bool aes) const {
  std::string key;
  if (spec_.revision >= 5) {
    key = file_key_;
  } else {
    // Algorithm 1: the per-object key.
    std::string input = file_key_;
    input.push_back(static_cast<char>(number & 0xFFU));
    input.push_back(static_cast<char>((number >> 8U) & 0xFFU));
    input.push_back(static_cast<char>((number >> 16U) & 0xFFU));
    input.push_back(static_cast<char>(generation & 0xFFU));
    input.push_back(static_cast<char>((generation >> 8U) & 0xFFU));
    if (aes) {
      input += "sAlT";
    }
    key = md5(input).substr(0, std::min<std::size_t>(file_key_.size() + 5, 16));
  }
  if (!aes) {
    return rc4(key, bytes);
  }
  std::string iv;
  for (unsigned i = 0; i < 16; ++i) {
    iv.push_back(static_cast<char>((number * 7U + generation + i * 13U) & 0xFFU));
  }
  const std::vector<std::byte> padded_data = Aes::pkcs7_pad(span(bytes));
  return iv + text(Aes::cbc_encrypt(span(key), span(iv), padded_data));
}

std::string PdfEncryptor::encrypt_string(std::uint32_t number, std::uint32_t generation,
                                         const std::string& bytes) const {
  const std::string& filter = spec_.string_filter;
  if (spec_.revision == 4 && filter == "Identity") {
    return bytes;
  }
  const bool aes = spec_.revision >= 5 || (spec_.revision == 4 && spec_.method == Method::aes);
  return encrypt(number, generation, bytes, aes);
}

std::string PdfEncryptor::encrypt_stream(std::uint32_t number, std::uint32_t generation,
                                         const std::string& bytes, bool is_metadata) const {
  if (spec_.revision == 4 && spec_.stream_filter == "Identity") {
    return bytes;
  }
  if (is_metadata && !spec_.encrypt_metadata) {
    return bytes;
  }
  const bool aes = spec_.revision >= 5 || (spec_.revision == 4 && spec_.method == Method::aes);
  return encrypt(number, generation, bytes, aes);
}

}  // namespace pdfkit::test
