// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The identifier from the output's own SHA-256, written in upper-case
// hexadecimal.

#include "writer/document_id.hpp"

#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"

#include <string_view>

namespace pdfkit::writer {

std::string DocumentId::placeholder() {
  return std::string(digits, '0');
}

std::string DocumentId::patch(std::string& file, std::size_t first, std::size_t second) {
  if (first + digits > file.size() || second + digits > file.size()) {
    throw platform::Error(platform::ErrorCode::internal,
                          "the /ID placeholder lies outside the written file",
                          "the cross-reference stream's /ID inside the output",
                          "this is a defect in pdfkit; report it with the input file");
  }
  const auto hash = platform::crypto::Sha2::digest(platform::crypto::Sha2::Variant::sha256,
                                                   platform::as_bytes(file));
  constexpr std::string_view hex = "0123456789ABCDEF";
  std::string text;
  text.reserve(digits);
  for (std::size_t i = 0; i < digits / 2; ++i) {
    const auto byte = std::to_integer<unsigned>(hash[i]);
    text += hex[byte >> 4U];
    text += hex[byte & 0x0FU];
  }
  file.replace(first, digits, text);
  file.replace(second, digits, text);
  return text;
}

}  // namespace pdfkit::writer
