// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Cmap is one CMap as ISO 32000-1 sections 9.7.5 and 9.10.3 (read) define
// it, docs/DESIGN.md section 5.3: the codespace ranges that decide how many
// bytes a code takes, the code-to-CID mappings of an encoding CMap and the
// code-to-Unicode mappings of a /ToUnicode CMap. Codes are matched against
// the codespace ranges shortest first, comparing the code's value with each
// range of that length, as pdf.js src/core/cmap.js readCharCode does (read);
// a byte sequence no range accepts takes the length of the shortest range.
// A Unicode destination is kept as the UTF-16BE bytes the map wrote, so a
// surrogate pair stays whole, and is turned into UTF-8 on lookup.

#ifndef PDFKIT_FONTS_CMAP_HPP
#define PDFKIT_FONTS_CMAP_HPP

#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::fonts {

class Cmap {
 public:
  struct Code {
    std::uint32_t value = 0;
    // The bytes the code takes, 1 to 4.
    std::size_t length = 1;
    // Whether a codespace range accepted the bytes.
    bool in_codespace = true;
  };

  // The identity CMap of a two-byte code equal to its CID (Identity-H and
  // Identity-V).
  [[nodiscard]] static Cmap identity(bool vertical);

  // Adds a codespace range of bytes 1 to 4 (low and high have that length).
  void add_codespace(std::uint32_t low, std::uint32_t high, std::size_t bytes);
  // Maps codes low to high to consecutive CIDs from cid.
  void add_cid_range(std::uint32_t low, std::uint32_t high, std::uint32_t cid);
  // Maps codes low to high to the UTF-16BE destination start, its last byte
  // incremented per code (with a carry into the byte before it, as pdf.js
  // mapBfRange does).
  void add_unicode_range(std::uint32_t low, std::uint32_t high, std::string start);
  // Maps one code to a UTF-16BE destination.
  void add_unicode(std::uint32_t code, std::string destination);
  // Takes the codespace and every mapping of parent that this map does not
  // define itself (usecmap).
  void inherit(const Cmap& parent);

  void set_vertical(bool vertical) noexcept { vertical_ = vertical; }
  [[nodiscard]] bool vertical() const noexcept { return vertical_; }

  // The code that starts at bytes[at] (at < bytes.size()); with no codespace,
  // a one-byte code.
  [[nodiscard]] Code next(platform::ByteSpan bytes, std::size_t at) const noexcept;
  // The CID of a code, or no value when no CID mapping covers it.
  [[nodiscard]] std::optional<std::uint32_t> cid(std::uint32_t code) const noexcept;
  // The UTF-16BE destination of a code, or no value.
  [[nodiscard]] std::optional<std::string> unicode_bytes(std::uint32_t code) const;
  // The destination as UTF-8, a surrogate pair joined, an unpaired surrogate
  // dropped; empty when the destination holds no character.
  [[nodiscard]] std::optional<std::string> unicode(std::uint32_t code) const;

  // Entries the parser skipped as malformed.
  [[nodiscard]] std::size_t malformed() const noexcept { return malformed_; }
  void count_malformed(std::size_t count = 1) noexcept { malformed_ += count; }

  // The UTF-8 form of UTF-16BE bytes (an odd length gains a leading zero
  // byte, as pdf.js reads it); an unpaired surrogate is dropped.
  [[nodiscard]] static std::string utf16be_to_utf8(const std::string& bytes);

 private:
  struct Codespace {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::size_t bytes = 1;
  };
  struct CidRange {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::uint32_t cid = 0;
  };
  struct UnicodeRange {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::string start;
  };

  std::vector<Codespace> codespaces_;
  // Later definitions take precedence, so lookups scan from the back.
  std::vector<CidRange> cid_ranges_;
  std::map<std::uint32_t, std::string> unicode_codes_;
  std::vector<UnicodeRange> unicode_ranges_;
  bool vertical_ = false;
  std::size_t malformed_ = 0;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_CMAP_HPP
