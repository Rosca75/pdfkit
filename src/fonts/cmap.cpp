// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The mappings are kept as the CMap wrote them (ranges, not expanded), so an
// identity range over 65,536 codes costs one entry; a lookup scans the ranges
// from the last defined, which the font caches per code.

#include "fonts/cmap.hpp"

#include "platform/utf8_sequence.hpp"

#include <algorithm>
#include <limits>
#include <ranges>

namespace pdfkit::fonts {

Cmap Cmap::identity(bool vertical) {
  Cmap map;
  map.add_codespace(0x0000, 0xFFFF, 2);
  map.add_cid_range(0x0000, 0xFFFF, 0);
  map.vertical_ = vertical;
  return map;
}

void Cmap::add_codespace(std::uint32_t low, std::uint32_t high, std::size_t bytes) {
  if (bytes < 1 || bytes > 4 || low > high) {
    ++malformed_;
    return;
  }
  codespaces_.push_back({low, high, bytes});
  std::ranges::stable_sort(
      codespaces_, [](const Codespace& a, const Codespace& b) { return a.bytes < b.bytes; });
}

void Cmap::add_cid_range(std::uint32_t low, std::uint32_t high, std::uint32_t cid) {
  if (low > high) {
    ++malformed_;
    return;
  }
  cid_ranges_.push_back({low, high, cid});
}

void Cmap::add_unicode_range(std::uint32_t low, std::uint32_t high, std::string start) {
  if (low > high || start.empty()) {
    ++malformed_;
    return;
  }
  unicode_ranges_.push_back({low, high, std::move(start)});
}

void Cmap::add_unicode(std::uint32_t code, std::string destination) {
  unicode_codes_[code] = std::move(destination);
}

void Cmap::inherit(const Cmap& parent) {
  if (codespaces_.empty()) {
    codespaces_ = parent.codespaces_;
  }
  // The parent's mappings come first, so this map's own entries, scanned
  // first from the back, take precedence.
  std::vector<CidRange> cids = parent.cid_ranges_;
  cids.insert(cids.end(), cid_ranges_.begin(), cid_ranges_.end());
  cid_ranges_ = std::move(cids);
  std::vector<UnicodeRange> ranges = parent.unicode_ranges_;
  ranges.insert(ranges.end(), unicode_ranges_.begin(), unicode_ranges_.end());
  unicode_ranges_ = std::move(ranges);
  for (const auto& [code, destination] : parent.unicode_codes_) {
    unicode_codes_.emplace(code, destination);
  }
  if (parent.vertical_) {
    vertical_ = true;
  }
}

Cmap::Code Cmap::next(platform::ByteSpan bytes, std::size_t at) const noexcept {
  if (codespaces_.empty() || at >= bytes.size()) {
    return {at < bytes.size() ? std::to_integer<std::uint32_t>(bytes[at]) : 0, 1, false};
  }
  std::uint32_t value = 0;
  for (std::size_t length = 1; length <= 4 && at + length <= bytes.size(); ++length) {
    value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[at + length - 1]);
    for (const Codespace& range : codespaces_) {
      if (range.bytes == length && value >= range.low && value <= range.high) {
        return {value, length, true};
      }
    }
  }
  // No range accepts the bytes: the code takes the shortest range's length.
  const std::size_t length = std::min(codespaces_.front().bytes, bytes.size() - at);
  value = 0;
  for (std::size_t i = 0; i < length; ++i) {
    value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[at + i]);
  }
  return {value, length, false};
}

std::optional<std::uint32_t> Cmap::cid(std::uint32_t code) const noexcept {
  for (const CidRange& range : std::views::reverse(cid_ranges_)) {
    if (code >= range.low && code <= range.high) {
      const std::uint64_t cid = static_cast<std::uint64_t>(range.cid) + (code - range.low);
      if (cid > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
      }
      return static_cast<std::uint32_t>(cid);
    }
  }
  return std::nullopt;
}

std::optional<std::string> Cmap::unicode_bytes(std::uint32_t code) const {
  if (const auto found = unicode_codes_.find(code); found != unicode_codes_.end()) {
    return found->second;
  }
  for (const UnicodeRange& range : std::views::reverse(unicode_ranges_)) {
    if (code < range.low || code > range.high) {
      continue;
    }
    // The start's last byte grows by the offset; a carry moves into the byte
    // before it, as pdf.js mapBfRange carries.
    std::string out = range.start;
    std::uint64_t carry = code - range.low;
    for (std::size_t i = out.size(); i > 0 && carry != 0; --i) {
      const std::uint64_t sum = static_cast<unsigned char>(out[i - 1]) + carry;
      out[i - 1] = static_cast<char>(sum & 0xFFU);
      carry = sum >> 8U;
    }
    return out;
  }
  return std::nullopt;
}

std::optional<std::string> Cmap::unicode(std::uint32_t code) const {
  const std::optional<std::string> bytes = unicode_bytes(code);
  if (!bytes) {
    return std::nullopt;
  }
  return utf16be_to_utf8(*bytes);
}

std::string Cmap::utf16be_to_utf8(const std::string& bytes) {
  std::string units = bytes;
  if (units.size() % 2 != 0) {
    units.insert(units.begin(), '\0');
  }
  std::string out;
  for (std::size_t i = 0; i + 1 < units.size(); i += 2) {
    const std::uint32_t unit =
        (static_cast<std::uint32_t>(static_cast<unsigned char>(units[i])) << 8U) |
        static_cast<unsigned char>(units[i + 1]);
    if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < units.size()) {
      const std::uint32_t low =
          (static_cast<std::uint32_t>(static_cast<unsigned char>(units[i + 2])) << 8U) |
          static_cast<unsigned char>(units[i + 3]);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        platform::append_utf8(
            out, static_cast<char32_t>(0x10000 + ((unit - 0xD800) << 10U) + (low - 0xDC00)));
        i += 2;
        continue;
      }
    }
    if (unit >= 0xD800 && unit <= 0xDFFF) {
      continue;
    }
    platform::append_utf8(out, static_cast<char32_t>(unit));
  }
  return out;
}

}  // namespace pdfkit::fonts
