// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Sha256 hashes the bytes of a file the dataset verb read, so each outcome
// names the bytes it describes. The CLI includes nothing of the library but
// its C header, and the C ABI has no hashing function, so the CLI carries its
// own SHA-256 (FIPS 180-4, recalled); the unit tier checks it against the
// library's tested SHA-2 and the dataset harness against every manifest hash.

#ifndef PDFKIT_CLI_SHA256_HPP
#define PDFKIT_CLI_SHA256_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace pdfkit::cli {

class Sha256 {
 public:
  // The digest of size bytes at data, as 64 lower-case hexadecimal digits.
  [[nodiscard]] static std::string hex(const std::uint8_t* data, std::size_t size);
};

}  // namespace pdfkit::cli

#endif  // PDFKIT_CLI_SHA256_HPP
