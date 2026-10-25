// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The RC4 stream cipher: the key-scheduling algorithm permutes a 256-byte state
// by the key, then each output byte swaps two state bytes and reads a third. The
// steps are those of the widely published algorithm (Schneier, "Applied
// Cryptography", 2nd edition, section 17.1; recalled), and the keystream vectors
// of RFC 6229 (https://www.rfc-editor.org/rfc/rfc6229.txt, read) confirm them.

#include "platform/crypto/rc4.hpp"

#include "platform/error.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace pdfkit::platform::crypto {

Rc4::Rc4(ByteSpan key) {
  if (key.empty() || key.size() > state_.size()) {
    throw Error(ErrorCode::invalid_argument,
                "an RC4 key of " + std::to_string(key.size()) + " bytes was given",
                "a key of 1 to 256 bytes",
                "pass the key the document's security handler derived; a key of another length "
                "means the encryption dictionary is damaged");
  }
  std::iota(state_.begin(), state_.end(), std::uint8_t{0});
  std::uint8_t j = 0;
  for (std::size_t i = 0; i < state_.size(); ++i) {
    const auto key_byte = std::to_integer<std::uint8_t>(key[i % key.size()]);
    j = static_cast<std::uint8_t>(j + state_[i] + key_byte);
    std::swap(state_[i], state_[j]);
  }
}

void Rc4::apply(std::span<std::byte> data) {
  for (std::byte& byte : data) {
    i_ = static_cast<std::uint8_t>(i_ + 1);
    j_ = static_cast<std::uint8_t>(j_ + state_[i_]);
    std::swap(state_[i_], state_[j_]);
    const std::uint8_t stream = state_[static_cast<std::uint8_t>(state_[i_] + state_[j_])];
    byte ^= static_cast<std::byte>(stream);
  }
}

std::vector<std::byte> Rc4::transform(ByteSpan key, ByteSpan data) {
  Rc4 rc4(key);
  std::vector<std::byte> out(data.begin(), data.end());
  rc4.apply(out);
  return out;
}

}  // namespace pdfkit::platform::crypto
