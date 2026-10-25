// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The three readings of a text string. PDFDocEncoding agrees with Latin-1 for
// the printable ASCII range and differs in 0x18 to 0x1F and 0x80 to 0x9F, so
// every code goes through the Annex D names rather than a Latin-1 shortcut.

#include "text/text_string.hpp"

#include "fonts/encodings.hpp"
#include "fonts/glyph_list.hpp"
#include "platform/utf8_sequence.hpp"

#include <cstdint>

namespace pdfkit::text {

namespace {

[[nodiscard]] std::string utf16(std::string_view bytes, bool big_endian) {
  std::string out;
  std::uint32_t pending = 0;
  for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
    const auto first = static_cast<std::uint8_t>(bytes[i]);
    const auto second = static_cast<std::uint8_t>(bytes[i + 1]);
    const std::uint32_t high = big_endian ? first : second;
    const std::uint32_t low = big_endian ? second : first;
    const std::uint32_t unit = (high << 8U) | low;
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      pending = unit;
      continue;
    }
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
      if (pending != 0) {
        const std::uint32_t value = 0x10000 + ((pending - 0xD800) << 10U) + (unit - 0xDC00);
        platform::append_utf8(out, static_cast<char32_t>(value));
      }
      pending = 0;
      continue;
    }
    pending = 0;
    platform::append_utf8(out, static_cast<char32_t>(unit));
  }
  return out;
}

[[nodiscard]] std::string pdf_doc(std::string_view bytes) {
  std::string out;
  for (const char byte : bytes) {
    const auto code = static_cast<std::uint8_t>(byte);
    if (code == '\t' || code == '\n' || code == '\r') {
      out.push_back(static_cast<char>(code));
      continue;
    }
    const std::string_view name = fonts::Encodings::name(fonts::Encodings::Base::pdf_doc, code);
    if (name.empty()) {
      continue;
    }
    if (const auto text = fonts::GlyphList::lookup(name)) {
      out += *text;
    }
  }
  return out;
}

}  // namespace

std::string TextString::to_utf8(std::string_view bytes) {
  if (bytes.size() >= 2 && static_cast<std::uint8_t>(bytes[0]) == 0xFE &&
      static_cast<std::uint8_t>(bytes[1]) == 0xFF) {
    return utf16(bytes.substr(2), true);
  }
  if (bytes.size() >= 2 && static_cast<std::uint8_t>(bytes[0]) == 0xFF &&
      static_cast<std::uint8_t>(bytes[1]) == 0xFE) {
    return utf16(bytes.substr(2), false);
  }
  if (bytes.size() >= 3 && static_cast<std::uint8_t>(bytes[0]) == 0xEF &&
      static_cast<std::uint8_t>(bytes[1]) == 0xBB && static_cast<std::uint8_t>(bytes[2]) == 0xBF) {
    std::string out;
    const std::string_view rest = bytes.substr(3);
    std::size_t at = 0;
    while (at < rest.size()) {
      const platform::Utf8Sequence sequence = platform::next_utf8_sequence(rest, at);
      if (sequence.valid) {
        out.append(rest.substr(at, sequence.length));
      }
      at += sequence.length;
    }
    return out;
  }
  return pdf_doc(bytes);
}

}  // namespace pdfkit::text
