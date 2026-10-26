// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The identifier from the output's own SHA-256, written in upper-case
// hexadecimal in /ID and in lower case in the XMP packet.

#include "writer/document_id.hpp"

#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace pdfkit::writer {

std::string DocumentId::placeholder() {
  return {std::string(digits, '0')};
}

std::string DocumentId::patch(std::string& file, std::size_t first, std::size_t second,
                              std::size_t packet_begin, std::size_t packet_end) {
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
  constexpr std::string_view lower = "0123456789abcdef";
  const auto identifier = [&](std::string_view placeholder, std::size_t offset) {
    std::string value(placeholder.substr(0, placeholder.size() - digits));
    for (std::size_t i = 0; i < digits / 2; ++i) {
      const auto byte = std::to_integer<unsigned>(hash[offset + i]);
      value += lower[byte >> 4U];
      value += lower[byte & 0x0FU];
    }
    return value;
  };
  packet_end = std::min(packet_end, file.size());
  for (const auto& [placeholder, offset] : {std::pair{xmp_document_placeholder, std::size_t{0}},
                                            std::pair{xmp_instance_placeholder, digits / 2}}) {
    const std::string value = identifier(placeholder, offset);
    std::size_t at = packet_begin;
    while (at < packet_end) {
      at = file.find(placeholder, at);
      if (at == std::string::npos || at + placeholder.size() > packet_end) {
        break;
      }
      file.replace(at, placeholder.size(), value);
      at += placeholder.size();
    }
  }
  return text;
}

}  // namespace pdfkit::writer
