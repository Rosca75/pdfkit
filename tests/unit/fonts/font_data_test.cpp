// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the two generated tables: their entry counts, their sort order
// (the lookups are binary searches), and the SHA-256 of their canonical lines
// recomputed from the compiled arrays against the digest the generator
// recorded, so a hand edit of a generated file fails here as well as in
// build/check_generated.py.

#include "fonts/generated/core14_table.hpp"
#include "fonts/generated/glyph_list_table.hpp"
#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"

#include <doctest.h>

#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace {

using pdfkit::fonts::generated::Core14Table;
using pdfkit::fonts::generated::GlyphListTable;
using pdfkit::platform::crypto::Sha2;

std::string hex(const std::vector<std::byte>& digest) {
  static const char* const digits = "0123456789abcdef";
  std::string out;
  for (const std::byte value : digest) {
    out.push_back(digits[std::to_integer<unsigned>(value) >> 4U]);
    out.push_back(digits[std::to_integer<unsigned>(value) & 0x0FU]);
  }
  return out;
}

std::string sha256(const std::string& text) {
  return hex(Sha2::digest(Sha2::Variant::sha256, pdfkit::platform::as_bytes(text)));
}

std::string code_point(char32_t value) {
  std::array<char, 16> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%04X", static_cast<unsigned>(value));
  return buffer.data();
}

std::string canonical(std::span<const GlyphListTable::Entry> entries) {
  std::string out;
  for (const GlyphListTable::Entry& entry : entries) {
    out += std::string(entry.name) + ";";
    for (std::size_t i = 0; i < entry.text.size(); ++i) {
      out += (i == 0 ? "" : " ") + code_point(entry.text[i]);
    }
    out += '\n';
  }
  return out;
}

TEST_CASE("the glyph lists hold their published entry counts in sorted order") {
  CHECK(GlyphListTable::adobe_glyph_list().size() == 4281U);
  CHECK(GlyphListTable::zapf_dingbats().size() == 201U);
  for (const auto table : {GlyphListTable::adobe_glyph_list(), GlyphListTable::zapf_dingbats()}) {
    for (std::size_t i = 1; i < table.size(); ++i) {
      REQUIRE(table[i - 1].name < table[i].name);
    }
  }
}

TEST_CASE("the glyph lists hash to the digests their generator recorded") {
  CHECK(sha256(canonical(GlyphListTable::adobe_glyph_list())) ==
        GlyphListTable::adobe_glyph_list_sha256());
  CHECK(sha256(canonical(GlyphListTable::zapf_dingbats())) ==
        GlyphListTable::zapf_dingbats_sha256());
}

TEST_CASE("the Core 14 table holds fourteen fonts, sorted glyphs and its recorded digest") {
  REQUIRE(Core14Table::fonts().size() == 14U);
  CHECK(Core14Table::fonts()[0].name == "Times-Roman");
  CHECK(Core14Table::fonts()[13].name == "ZapfDingbats");
  std::size_t total = 0;
  std::string lines;
  for (const Core14Table::Font& font : Core14Table::fonts()) {
    CHECK(font.first == total);
    total += font.count;
    lines += "F " + std::string(font.name) + " " + std::to_string(font.ascender) + " " +
             std::to_string(font.descender) + " " + std::to_string(font.bbox[0]) + " " +
             std::to_string(font.bbox[1]) + " " + std::to_string(font.bbox[2]) + " " +
             std::to_string(font.bbox[3]) + " " + std::to_string(font.count) + "\n";
    for (std::size_t i = font.first; i < std::size_t{font.first} + font.count; ++i) {
      const Core14Table::Glyph& glyph = Core14Table::glyphs()[i];
      if (i > font.first) {
        REQUIRE(Core14Table::glyphs()[i - 1].name < glyph.name);
      }
      lines += std::string(glyph.name) + " " + std::to_string(glyph.code) + " " +
               std::to_string(glyph.width) + "\n";
    }
  }
  CHECK(total == Core14Table::glyphs().size());
  // 315 glyphs in each text face, 190 in Symbol, 202 in ZapfDingbats.
  CHECK(total == 12U * 315U + 190U + 202U);
  CHECK(sha256(lines) == Core14Table::content_sha256());
}

}  // namespace
