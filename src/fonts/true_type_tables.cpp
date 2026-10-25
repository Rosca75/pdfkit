// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Each table is read through the bounds-checked big-endian readers of the
// platform layer, so a short table yields no value instead of a byte from
// elsewhere. The cmap subtables keep their own bytes and are looked up on
// demand; the post names, the advances, the empty-glyph flags and the
// glyph-to-Unicode map are read once. The 258 standard Macintosh glyph names
// of post formats 1 and 2 are ported from pdf.js src/core/fonts_utils.js
// MacStandardGlyphOrdering (Apache-2.0, read at commit
// e2a5e1d34517b7e544f234d54eec3aa37df0b846).

#include "fonts/true_type_tables.hpp"

#include <algorithm>
#include <array>
#include <span>

namespace pdfkit::fonts {

namespace {

using platform::ByteSpan;
using platform::read_be;

// clang-format off
constexpr std::array<std::string_view, 258> mac_standard_names = {
    ".notdef", ".null", "nonmarkingreturn", "space", "exclam", "quotedbl", "numbersign",
    "dollar", "percent", "ampersand", "quotesingle", "parenleft", "parenright", "asterisk",
    "plus", "comma", "hyphen", "period", "slash", "zero", "one", "two", "three", "four", "five",
    "six", "seven", "eight", "nine", "colon", "semicolon", "less", "equal", "greater",
    "question", "at", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
    "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "bracketleft", "backslash",
    "bracketright", "asciicircum", "underscore", "grave", "a", "b", "c", "d", "e", "f", "g", "h",
    "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
    "braceleft", "bar", "braceright", "asciitilde", "Adieresis", "Aring", "Ccedilla", "Eacute",
    "Ntilde", "Odieresis", "Udieresis", "aacute", "agrave", "acircumflex", "adieresis", "atilde",
    "aring", "ccedilla", "eacute", "egrave", "ecircumflex", "edieresis", "iacute", "igrave",
    "icircumflex", "idieresis", "ntilde", "oacute", "ograve", "ocircumflex", "odieresis",
    "otilde", "uacute", "ugrave", "ucircumflex", "udieresis", "dagger", "degree", "cent",
    "sterling", "section", "bullet", "paragraph", "germandbls", "registered", "copyright",
    "trademark", "acute", "dieresis", "notequal", "AE", "Oslash", "infinity", "plusminus",
    "lessequal", "greaterequal", "yen", "mu", "partialdiff", "summation", "product", "pi",
    "integral", "ordfeminine", "ordmasculine", "Omega", "ae", "oslash", "questiondown",
    "exclamdown", "logicalnot", "radical", "florin", "approxequal", "Delta", "guillemotleft",
    "guillemotright", "ellipsis", "nonbreakingspace", "Agrave", "Atilde", "Otilde", "OE", "oe",
    "endash", "emdash", "quotedblleft", "quotedblright", "quoteleft", "quoteright", "divide",
    "lozenge", "ydieresis", "Ydieresis", "fraction", "currency", "guilsinglleft",
    "guilsinglright", "fi", "fl", "daggerdbl", "periodcentered", "quotesinglbase",
    "quotedblbase", "perthousand", "Acircumflex", "Ecircumflex", "Aacute", "Edieresis", "Egrave",
    "Iacute", "Icircumflex", "Idieresis", "Igrave", "Oacute", "Ocircumflex", "apple", "Ograve",
    "Uacute", "Ucircumflex", "Ugrave", "dotlessi", "circumflex", "tilde", "macron", "breve",
    "dotaccent", "ring", "cedilla", "hungarumlaut", "ogonek", "caron", "Lslash", "lslash",
    "Scaron", "scaron", "Zcaron", "zcaron", "brokenbar", "Eth", "eth", "Yacute", "yacute",
    "Thorn", "thorn", "minus", "multiply", "onesuperior", "twosuperior", "threesuperior",
    "onehalf", "onequarter", "threequarters", "franc", "Gbreve", "gbreve", "Idotaccent",
    "Scedilla", "scedilla", "Cacute", "cacute", "Ccaron", "ccaron", "dcroat",
};
// clang-format on

constexpr std::uint32_t tag(const char (&text)[5]) noexcept {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(text[0])) << 24U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(text[1])) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(text[2])) << 8U) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(text[3]));
}

std::uint16_t u16(ByteSpan data, std::size_t at) noexcept {
  return read_be<std::uint16_t>(data, at).value_or(0);
}

std::uint32_t u32(ByteSpan data, std::size_t at) noexcept {
  return read_be<std::uint32_t>(data, at).value_or(0);
}

std::int16_t i16(ByteSpan data, std::size_t at) noexcept {
  return static_cast<std::int16_t>(u16(data, at));
}

// The bytes [offset, offset + length) of data, clipped to what data holds.
ByteSpan slice(ByteSpan data, std::size_t offset, std::size_t length) noexcept {
  if (offset >= data.size()) {
    return {};
  }
  return data.subspan(offset, std::min(length, data.size() - offset));
}

// The Unicode subtables in order of preference: full repertoire, then BMP.
constexpr std::array<std::array<std::uint16_t, 2>, 7> unicode_subtables = {{
    {3, 10},
    {0, 6},
    {0, 4},
    {3, 1},
    {0, 3},
    {0, 1},
    {0, 0},
}};

}  // namespace

bool TrueTypeTables::looks_like(ByteSpan data) noexcept {
  const std::uint32_t version = u32(data, 0);
  return version == 0x00010000 || version == tag("true") || version == tag("OTTO") ||
         version == tag("ttcf") || version == tag("typ1");
}

std::optional<TrueTypeTables> TrueTypeTables::parse(ByteSpan data, platform::Budget& budget) {
  if (!looks_like(data)) {
    return std::nullopt;
  }
  std::size_t directory = 0;
  if (u32(data, 0) == tag("ttcf")) {
    // A collection: the first font's directory.
    directory = u32(data, 12);
    if (!looks_like(slice(data, directory, 4))) {
      return std::nullopt;
    }
  }
  const std::uint16_t count = u16(data, directory + 4);
  if (directory + 12 + std::size_t{count} * 16 > data.size()) {
    return std::nullopt;
  }
  std::map<std::uint32_t, ByteSpan> tables;
  for (std::uint16_t i = 0; i < count; ++i) {
    const std::size_t record = directory + 12 + std::size_t{i} * 16;
    const std::uint32_t name = u32(data, record);
    const std::uint32_t offset = u32(data, record + 8);
    const std::uint32_t length = u32(data, record + 12);
    if (offset < data.size()) {
      tables.emplace(name, slice(data, offset, length));
    }
  }
  const auto table = [&tables](std::uint32_t name) -> ByteSpan {
    const auto found = tables.find(name);
    return found == tables.end() ? ByteSpan() : found->second;
  };

  TrueTypeTables out;
  const ByteSpan head = table(tag("head"));
  bool long_offsets = false;
  if (head.size() >= 54) {
    const std::uint16_t units = u16(head, 18);
    if (units >= 16 && units <= 16384) {
      out.units_per_em_ = units;
    }
    long_offsets = i16(head, 50) != 0;
  }
  const ByteSpan maxp = table(tag("maxp"));
  if (maxp.size() >= 6) {
    out.glyph_count_ = u16(maxp, 4);
  }
  out.read_metrics(table(tag("hhea")), table(tag("hmtx")), budget);
  const ByteSpan os2 = table(tag("OS/2"));
  if (out.ascender_ == 0 && out.descender_ == 0 && os2.size() >= 72) {
    out.ascender_ = i16(os2, 68);
    out.descender_ = i16(os2, 70);
  }
  out.read_cmap(table(tag("cmap")), budget);
  out.read_post(table(tag("post")), budget);
  out.read_outlines(table(tag("loca")), table(tag("glyf")), long_offsets, budget);
  const ByteSpan cff = table(tag("CFF "));
  if (!cff.empty()) {
    budget.check_claim(cff.size(), "the CFF table of a font program");
    out.cff_.assign(cff.begin(), cff.end());
  }
  out.build_unicode(budget);
  return out;
}

void TrueTypeTables::read_metrics(ByteSpan hhea, ByteSpan hmtx, platform::Budget& budget) {
  if (hhea.size() < 36) {
    return;
  }
  ascender_ = i16(hhea, 4);
  descender_ = i16(hhea, 6);
  const std::size_t metrics = std::min<std::size_t>(u16(hhea, 34), hmtx.size() / 4);
  budget.check_claim(metrics * sizeof(std::uint16_t), "the hmtx table of a font program");
  advances_.reserve(metrics);
  for (std::size_t i = 0; i < metrics; ++i) {
    advances_.push_back(u16(hmtx, i * 4));
  }
}

void TrueTypeTables::read_cmap(ByteSpan table, platform::Budget& budget) {
  const std::uint16_t count = u16(table, 2);
  for (std::uint16_t i = 0; i < count; ++i) {
    const std::size_t record = 4 + std::size_t{i} * 8;
    if (record + 8 > table.size()) {
      break;
    }
    const std::uint16_t platform_id = u16(table, record);
    const std::uint16_t encoding_id = u16(table, record + 2);
    const std::uint32_t offset = u32(table, record + 4);
    const std::uint16_t format = u16(table, offset);
    std::size_t length = 0;
    if (format == 0 || format == 4 || format == 6) {
      length = u16(table, offset + 2);
    } else if (format == 12) {
      length = u32(table, offset + 4);
    } else {
      continue;
    }
    const ByteSpan bytes = slice(table, offset, length);
    if (bytes.size() < 6 || has_cmap(platform_id, encoding_id)) {
      continue;
    }
    budget.check_claim(bytes.size(), "a cmap subtable of a font program");
    subtables_.push_back({platform_id, encoding_id, format, {bytes.begin(), bytes.end()}});
  }
}

void TrueTypeTables::read_post(ByteSpan table, platform::Budget& budget) {
  const std::uint32_t format = u32(table, 0);
  if (format == 0x00010000) {
    const std::size_t count = std::min<std::size_t>(glyph_count_ == 0 ? 258 : glyph_count_, 258);
    names_.assign(mac_standard_names.begin(),
                  mac_standard_names.begin() + static_cast<std::ptrdiff_t>(count));
  } else if (format == 0x00020000 && table.size() >= 34) {
    const std::size_t count = u16(table, 32);
    if (34 + count * 2 > table.size()) {
      return;
    }
    budget.check_claim(count * 2, "the post table of a font program");
    // The custom names follow the index array as Pascal strings.
    std::vector<std::string> custom;
    std::size_t at = 34 + count * 2;
    while (at < table.size()) {
      const auto length = std::to_integer<std::size_t>(table[at]);
      if (at + 1 + length > table.size()) {
        break;
      }
      std::string name;
      name.reserve(length);
      for (std::size_t k = 0; k < length; ++k) {
        name.push_back(static_cast<char>(table[at + 1 + k]));
      }
      custom.push_back(std::move(name));
      at += 1 + length;
    }
    names_.reserve(count);
    for (std::size_t glyph = 0; glyph < count; ++glyph) {
      const std::uint16_t index = u16(table, 34 + glyph * 2);
      if (index < mac_standard_names.size()) {
        names_.emplace_back(mac_standard_names[index]);
      } else if (index - mac_standard_names.size() < custom.size()) {
        names_.push_back(custom[index - mac_standard_names.size()]);
      } else {
        names_.emplace_back();
      }
    }
  }
  for (std::size_t glyph = 0; glyph < names_.size(); ++glyph) {
    if (!names_[glyph].empty()) {
      glyph_by_name_.emplace(names_[glyph], static_cast<std::uint32_t>(glyph));
    }
  }
}

void TrueTypeTables::read_outlines(ByteSpan loca, ByteSpan glyf, bool long_offsets,
                                   platform::Budget& budget) {
  if (loca.empty() || glyf.empty() || glyph_count_ == 0) {
    return;
  }
  const std::size_t entry = long_offsets ? 4 : 2;
  // loca holds one offset more than there are glyphs.
  const std::size_t count =
      std::min<std::size_t>(std::size_t{glyph_count_} + 1, loca.size() / entry);
  if (count < 2) {
    return;
  }
  budget.check_claim(count, "the loca table of a font program");
  empty_.assign(count - 1, true);
  const auto offset = [&](std::size_t glyph) -> std::uint64_t {
    return long_offsets ? u32(loca, glyph * 4) : std::uint64_t{u16(loca, glyph * 2)} * 2;
  };
  for (std::size_t glyph = 0; glyph + 1 < count; ++glyph) {
    const std::uint64_t start = offset(glyph);
    const std::uint64_t end = offset(glyph + 1);
    empty_[glyph] = end <= start || start >= glyf.size();
  }
}

void TrueTypeTables::build_unicode(platform::Budget& budget) {
  if (glyph_count_ == 0) {
    return;
  }
  // Every Unicode subtable contributes, the preferred first: a glyph keeps
  // the code point its most preferred subtable gives it, the lowest there.
  bool any = false;
  for (const auto& [platform_id, encoding_id] : unicode_subtables) {
    for (const Subtable& table : subtables_) {
      if (table.platform != platform_id || table.encoding != encoding_id) {
        continue;
      }
      if (!any) {
        budget.check_claim(std::size_t{glyph_count_} * sizeof(char32_t),
                           "the glyph count of a font program");
        unicode_.assign(glyph_count_, U'\0');
        any = true;
      }
      add_unicode(table);
    }
  }
}

void TrueTypeTables::add_unicode(const Subtable& table) {
  std::vector<char32_t> found(unicode_.size(), U'\0');
  const auto note = [&found](std::uint32_t code, std::uint32_t glyph) {
    if (glyph != 0 && glyph < found.size() && found[glyph] == U'\0') {
      found[glyph] = static_cast<char32_t>(code);
    }
  };
  const ByteSpan data(table.bytes);
  if (table.format == 4) {
    // Each segment is walked directly with the lookup's arithmetic.
    const std::size_t segments = u16(data, 6) / 2;
    for (std::size_t i = 0; i < segments; ++i) {
      const std::uint16_t end = u16(data, 14 + i * 2);
      const std::size_t start_at = 16 + segments * 2 + i * 2;
      const std::uint16_t start = u16(data, start_at);
      const std::uint16_t delta = u16(data, start_at + segments * 2);
      const std::size_t range_at = start_at + segments * 4;
      const std::uint16_t range_offset = u16(data, range_at);
      for (std::uint32_t code = start; code <= end && code != 0xFFFF; ++code) {
        std::uint32_t glyph = 0;
        if (range_offset == 0) {
          glyph = (code + delta) & 0xFFFFU;
        } else {
          const std::uint16_t raw =
              u16(data, range_at + range_offset + std::size_t{code - start} * 2);
          glyph = raw == 0 ? 0U : (std::uint32_t{raw} + delta) & 0xFFFFU;
        }
        note(code, glyph);
      }
    }
  } else if (table.format == 12) {
    const std::uint32_t groups = u32(data, 12);
    std::uint64_t visited = 0;
    for (std::uint32_t i = 0; i < groups && 16 + std::size_t{i} * 12 + 12 <= data.size(); ++i) {
      const std::size_t at = 16 + std::size_t{i} * 12;
      const std::uint32_t first = u32(data, at);
      const std::uint32_t last = std::min<std::uint32_t>(u32(data, at + 4), 0x10FFFF);
      const std::uint32_t glyph = u32(data, at + 8);
      for (std::uint32_t code = first; code <= last && visited < 0x110000; ++code, ++visited) {
        note(code, glyph + (code - first));
      }
    }
  } else if (table.format == 6) {
    const std::uint16_t first = u16(data, 6);
    const std::uint16_t entries = u16(data, 8);
    for (std::uint32_t i = 0; i < entries; ++i) {
      note(first + i, u16(data, 10 + std::size_t{i} * 2));
    }
  } else if (table.format == 0) {
    for (std::uint32_t code = 0; code < 256 && 6 + code < data.size(); ++code) {
      note(code, std::to_integer<std::uint32_t>(data[6 + code]));
    }
  }
  for (std::size_t glyph = 0; glyph < unicode_.size(); ++glyph) {
    if (unicode_[glyph] == U'\0') {
      unicode_[glyph] = found[glyph];
    }
  }
}

std::optional<std::uint16_t> TrueTypeTables::advance(std::uint32_t glyph) const noexcept {
  if (advances_.empty()) {
    return std::nullopt;
  }
  return glyph < advances_.size() ? advances_[glyph] : advances_.back();
}

bool TrueTypeTables::has_cmap(std::uint16_t platform, std::uint16_t encoding) const noexcept {
  return std::ranges::any_of(subtables_, [&](const Subtable& table) {
    return table.platform == platform && table.encoding == encoding;
  });
}

std::optional<std::uint32_t> TrueTypeTables::lookup(std::uint16_t platform, std::uint16_t encoding,
                                                    std::uint32_t code) const noexcept {
  for (const Subtable& table : subtables_) {
    if (table.platform == platform && table.encoding == encoding) {
      return map_code(table, code);
    }
  }
  return std::nullopt;
}

std::optional<std::uint32_t> TrueTypeTables::map_code(const Subtable& table,
                                                      std::uint32_t code) noexcept {
  const ByteSpan data(table.bytes);
  std::uint32_t glyph = 0;
  switch (table.format) {
    case 0:
      if (code < 256 && 6 + code < data.size()) {
        glyph = std::to_integer<std::uint32_t>(data[6 + code]);
      }
      break;
    case 4: {
      if (code > 0xFFFF) {
        return std::nullopt;
      }
      const std::size_t segments = u16(data, 6) / 2;
      for (std::size_t i = 0; i < segments; ++i) {
        const std::uint16_t end = u16(data, 14 + i * 2);
        if (code > end) {
          continue;
        }
        const std::size_t start_at = 16 + segments * 2 + i * 2;
        const std::uint16_t start = u16(data, start_at);
        if (code < start) {
          return std::nullopt;
        }
        const std::uint16_t delta = u16(data, start_at + segments * 2);
        const std::size_t range_at = start_at + segments * 4;
        const std::uint16_t range_offset = u16(data, range_at);
        if (range_offset == 0) {
          glyph = (code + delta) & 0xFFFFU;
        } else {
          const std::size_t at = range_at + range_offset + std::size_t{code - start} * 2;
          const std::uint16_t raw = u16(data, at);
          glyph = raw == 0 ? 0U : (std::uint32_t{raw} + delta) & 0xFFFFU;
        }
        break;
      }
      break;
    }
    case 6: {
      const std::uint16_t first = u16(data, 6);
      const std::uint16_t entries = u16(data, 8);
      if (code >= first && code - first < entries) {
        glyph = u16(data, 10 + std::size_t{code - first} * 2);
      }
      break;
    }
    case 12: {
      const std::uint32_t groups = u32(data, 12);
      for (std::uint32_t i = 0; i < groups && 16 + std::size_t{i} * 12 + 12 <= data.size(); ++i) {
        const std::size_t at = 16 + std::size_t{i} * 12;
        const std::uint32_t first = u32(data, at);
        const std::uint32_t last = u32(data, at + 4);
        if (code >= first && code <= last) {
          glyph = u32(data, at + 8) + (code - first);
          break;
        }
      }
      break;
    }
    default:
      break;
  }
  if (glyph == 0) {
    return std::nullopt;
  }
  return glyph;
}

std::optional<char32_t> TrueTypeTables::unicode_of(std::uint32_t glyph) const noexcept {
  if (glyph >= unicode_.size() || unicode_[glyph] == U'\0') {
    return std::nullopt;
  }
  return unicode_[glyph];
}

std::string_view TrueTypeTables::glyph_name(std::uint32_t glyph) const noexcept {
  return glyph < names_.size() ? std::string_view(names_[glyph]) : std::string_view();
}

std::optional<std::uint32_t> TrueTypeTables::glyph_by_name(std::string_view name) const {
  const auto found = glyph_by_name_.find(name);
  if (found == glyph_by_name_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<bool> TrueTypeTables::is_empty(std::uint32_t glyph) const noexcept {
  if (glyph >= empty_.size()) {
    return std::nullopt;
  }
  return empty_[glyph];
}

}  // namespace pdfkit::fonts
