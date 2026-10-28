// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TrueTypeTables reads what text extraction needs from a TrueType or
// OpenType program, docs/DESIGN.md section 5.7, and nothing that drawing
// needs: the table directory (a collection's first font), the cmap subtables
// of formats 0, 4, 6 and 12 by platform and encoding id, head's units per em
// and bounding box, hhea's ascender, descender and metric count, maxp's glyph
// count, hmtx's advances (the last advance repeating past the metric count),
// post's glyph names (formats 1 and 2, over the 258 standard Macintosh names),
// loca and glyf only to say whether a glyph is empty, and the bytes of a CFF
// table, which the CFF reader takes. The layouts are those of the OpenType
// specification (https://learn.microsoft.com/en-us/typography/opentype/spec/,
// recalled). Every offset and count is checked against its table's length; a
// table that does not fit is dropped and the rest of the program still
// answers.

#ifndef PDFKIT_FONTS_TRUE_TYPE_TABLES_HPP
#define PDFKIT_FONTS_TRUE_TYPE_TABLES_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::fonts {

class TrueTypeTables {
 public:
  // The program's tables, or no value when the bytes hold no table directory.
  // Each count is checked against budget before memory is reserved for it.
  [[nodiscard]] static std::optional<TrueTypeTables> parse(platform::ByteSpan data,
                                                           platform::Budget& budget);

  // Whether the bytes start like an sfnt (TrueType, OpenType or a collection).
  [[nodiscard]] static bool looks_like(platform::ByteSpan data) noexcept;

  [[nodiscard]] std::uint16_t units_per_em() const noexcept { return units_per_em_; }
  [[nodiscard]] std::uint32_t glyph_count() const noexcept { return glyph_count_; }
  // The advance of a glyph in font units, or no value without hmtx.
  [[nodiscard]] std::optional<std::uint16_t> advance(std::uint32_t glyph) const noexcept;
  // hhea's ascender and descender in font units (OS/2's typographic values
  // when hhea gives none); zero when neither table answers.
  [[nodiscard]] std::int16_t ascender() const noexcept { return ascender_; }
  [[nodiscard]] std::int16_t descender() const noexcept { return descender_; }

  // Whether a cmap subtable of that platform and encoding was read.
  [[nodiscard]] bool has_cmap(std::uint16_t platform, std::uint16_t encoding) const noexcept;
  // The glyph a subtable maps code to; no value for glyph 0 or no subtable.
  [[nodiscard]] std::optional<std::uint32_t> lookup(std::uint16_t platform, std::uint16_t encoding,
                                                    std::uint32_t code) const noexcept;
  // The Unicode code point the Unicode subtables ((3,10), (0,6), (0,4),
  // (3,1), (0,3) to (0,0), in that preference) map to glyph: the lowest the
  // most preferred subtable gives; no value when none does.
  [[nodiscard]] std::optional<char32_t> unicode_of(std::uint32_t glyph) const noexcept;

  // The post name of a glyph; empty without one.
  [[nodiscard]] std::string_view glyph_name(std::uint32_t glyph) const noexcept;
  // The first glyph post names name; no value when none does.
  [[nodiscard]] std::optional<std::uint32_t> glyph_by_name(std::string_view name) const;
  // Whether glyf holds no outline for glyph; no value without loca and glyf.
  [[nodiscard]] std::optional<bool> is_empty(std::uint32_t glyph) const noexcept;

  // The bytes of the CFF table of an OpenType program; empty without one.
  [[nodiscard]] const std::vector<std::byte>& cff() const noexcept { return cff_; }

 private:
  struct Subtable {
    std::uint16_t platform = 0;
    std::uint16_t encoding = 0;
    std::uint16_t format = 0;
    std::vector<std::byte> bytes;
  };

  [[nodiscard]] static std::optional<std::uint32_t> map_code(const Subtable& table,
                                                             std::uint32_t code) noexcept;
  void read_cmap(platform::ByteSpan table, platform::Budget& budget);
  void read_post(platform::ByteSpan table, platform::Budget& budget);
  void read_metrics(platform::ByteSpan hhea, platform::ByteSpan hmtx, platform::Budget& budget);
  void read_outlines(platform::ByteSpan loca, platform::ByteSpan glyf, bool long_offsets,
                     platform::Budget& budget);
  void build_unicode(platform::Budget& budget);
  void add_unicode(const Subtable& table);

  std::uint16_t units_per_em_ = 1000;
  std::uint32_t glyph_count_ = 0;
  std::int16_t ascender_ = 0;
  std::int16_t descender_ = 0;
  std::vector<std::uint16_t> advances_;
  std::vector<Subtable> subtables_;
  std::vector<std::string> names_;
  std::map<std::string, std::uint32_t, std::less<>> glyph_by_name_;
  std::vector<bool> empty_;
  std::vector<char32_t> unicode_;
  std::vector<std::byte> cff_;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_TRUE_TYPE_TABLES_HPP
