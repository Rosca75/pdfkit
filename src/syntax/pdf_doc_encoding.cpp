// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The PDFDoc table of ISO 32000-1 Annex D (read): printable ASCII as itself,
// tab, line feed and carriage return kept, eight spacing accents at 0x18 to
// 0x1F, typographic marks and Latin letters at 0x80 to 0xA0, and Latin-1 from
// 0xA1 on, 0xAD (the soft hyphen) and 0x7F and 0x9F left undefined.

#include "syntax/pdf_doc_encoding.hpp"

#include "platform/utf8_sequence.hpp"

#include <array>
#include <cstddef>

namespace pdfkit::syntax {

namespace {

// 0x18 to 0x1F: breve, caron, circumflex, dot accent, hungarumlaut, ogonek,
// ring, tilde.
constexpr std::array<char32_t, 8> accents = {0x02D8, 0x02C7, 0x02C6, 0x02D9,
                                             0x02DD, 0x02DB, 0x02DA, 0x02DC};

// 0x80 to 0xA0: bullet, dagger, double dagger, ellipsis, em dash, en dash,
// florin, fraction, single guillemets, minus, per mille, the low and the
// curly double quotes, the curly single quotes and the low single quote,
// trademark, the fi and fl ligatures, Lslash, OE, Scaron, Ydieresis,
// Zcaron, dotless i, lslash, oe, scaron, zcaron, (0x9F undefined), Euro.
constexpr std::array<char32_t, 33> marks = {
    0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044, 0x2039, 0x203A, 0x2212,
    0x2030, 0x201E, 0x201C, 0x201D, 0x2018, 0x2019, 0x201A, 0x2122, 0xFB01, 0xFB02, 0x0141,
    0x0152, 0x0160, 0x0178, 0x017D, 0x0131, 0x0142, 0x0153, 0x0161, 0x017E, 0x0000, 0x20AC};

std::string utf16(std::string_view bytes, bool big_endian) {
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

bool starts_with(std::string_view bytes, std::initializer_list<std::uint8_t> mark) {
  if (bytes.size() < mark.size()) {
    return false;
  }
  std::size_t i = 0;
  for (const std::uint8_t byte : mark) {
    if (static_cast<std::uint8_t>(bytes[i++]) != byte) {
      return false;
    }
  }
  return true;
}

}  // namespace

char32_t PdfDocEncoding::code_point(std::uint8_t code) noexcept {
  if (code == '\t' || code == '\n' || code == '\r') {
    return code;
  }
  if (code >= 0x18 && code <= 0x1F) {
    return accents.at(code - 0x18U);
  }
  if (code >= 0x20 && code <= 0x7E) {
    return code;
  }
  if (code >= 0x80 && code <= 0xA0) {
    return marks.at(code - 0x80U);
  }
  if (code >= 0xA1 && code != 0xAD) {
    return code;
  }
  return 0;
}

PdfDocEncoding::Decoded PdfDocEncoding::decode(std::string_view bytes) {
  if (starts_with(bytes, {0xFE, 0xFF})) {
    return {utf16(bytes.substr(2), true), Form::utf16be};
  }
  if (starts_with(bytes, {0xFF, 0xFE})) {
    return {utf16(bytes.substr(2), false), Form::utf16le};
  }
  if (starts_with(bytes, {0xEF, 0xBB, 0xBF})) {
    const std::string_view rest = bytes.substr(3);
    std::string out;
    std::size_t at = 0;
    while (at < rest.size()) {
      const platform::Utf8Sequence sequence = platform::next_utf8_sequence(rest, at);
      if (sequence.valid) {
        out.append(rest.substr(at, sequence.length));
      }
      at += sequence.length;
    }
    return {out, Form::utf8};
  }
  std::string out;
  for (const char byte : bytes) {
    const char32_t value = code_point(static_cast<std::uint8_t>(byte));
    if (value != 0) {
      platform::append_utf8(out, value);
    }
  }
  return {out, Form::pdf_doc};
}

std::optional<std::string> PdfDocEncoding::encode(std::string_view utf8) {
  std::string out;
  std::size_t at = 0;
  while (at < utf8.size()) {
    const platform::Utf8Sequence sequence = platform::next_utf8_sequence(utf8, at);
    if (!sequence.valid) {
      return std::nullopt;
    }
    bool found = false;
    for (unsigned code = 1; code < 256 && !found; ++code) {
      if (code_point(static_cast<std::uint8_t>(code)) == sequence.code_point) {
        out.push_back(static_cast<char>(code));
        found = true;
      }
    }
    if (!found) {
      return std::nullopt;
    }
    at += sequence.length;
  }
  return out;
}

std::string PdfDocEncoding::utf16be(std::string_view utf8) {
  std::string out = "\xFE\xFF";
  const auto unit = [&out](std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<char>(value & 0xFFU));
  };
  std::size_t at = 0;
  while (at < utf8.size()) {
    const platform::Utf8Sequence sequence = platform::next_utf8_sequence(utf8, at);
    const std::uint32_t value = sequence.valid ? sequence.code_point : 0xFFFDU;
    if (value >= 0x10000) {
      const std::uint32_t offset = value - 0x10000;
      unit(0xD800 + (offset >> 10U));
      unit(0xDC00 + (offset & 0x3FFU));
    } else {
      unit(value);
    }
    at += sequence.length;
  }
  return out;
}

std::string PdfDocEncoding::text_string(std::string_view utf8) {
  if (std::optional<std::string> bytes = encode(utf8)) {
    // A PDFDoc string that would read as a byte-order mark is written as
    // UTF-16BE instead, so it reads back as written.
    if (!starts_with(*bytes, {0xFE, 0xFF}) && !starts_with(*bytes, {0xFF, 0xFE}) &&
        !starts_with(*bytes, {0xEF, 0xBB, 0xBF})) {
      return *std::move(bytes);
    }
  }
  return utf16be(utf8);
}

std::string_view PdfDocEncoding::name(Form form) noexcept {
  switch (form) {
    case Form::utf16be:
      return "utf16be";
    case Form::utf16le:
      return "utf16le";
    case Form::utf8:
      return "utf8";
    case Form::pdf_doc:
      break;
  }
  return "pdf_doc";
}

}  // namespace pdfkit::syntax
