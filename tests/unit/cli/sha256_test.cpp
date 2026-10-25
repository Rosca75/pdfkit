// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the CLI's own SHA-256 against the library's SHA-2, which is tested
// against NIST's published vectors, at every length around the padding
// boundaries and over a large buffer.

#include "cli/sha256.hpp"

#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::platform::crypto::Sha2;

std::string library_hex(const std::vector<std::uint8_t>& data) {
  static constexpr char digits[] = "0123456789abcdef";
  const std::vector<std::byte> digest =
      Sha2::digest(Sha2::Variant::sha256, std::as_bytes(std::span(data)));
  std::string out;
  for (const std::byte value : digest) {
    out.push_back(digits[std::to_integer<unsigned>(value) >> 4U]);
    out.push_back(digits[std::to_integer<unsigned>(value) & 0x0FU]);
  }
  return out;
}

TEST_CASE("the CLI's SHA-256 agrees with the library's at every padding boundary") {
  std::vector<std::uint8_t> data;
  for (std::size_t size = 0; size <= 200; ++size) {
    CAPTURE(size);
    CHECK(pdfkit::cli::Sha256::hex(data.data(), data.size()) == library_hex(data));
    data.push_back(static_cast<std::uint8_t>(size * 31U + 7U));
  }
  // "abc", FIPS 180-4's example (read in the library's tests).
  const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
  CHECK(pdfkit::cli::Sha256::hex(abc.data(), abc.size()) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::vector<std::uint8_t> large(1000003);
  for (std::size_t i = 0; i < large.size(); ++i) {
    large[i] = static_cast<std::uint8_t>(i ^ (i >> 8U));
  }
  CHECK(pdfkit::cli::Sha256::hex(large.data(), large.size()) == library_hex(large));
}

}  // namespace
