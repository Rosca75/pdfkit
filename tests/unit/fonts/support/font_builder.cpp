// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The writers emit big-endian fields by hand. The TrueType tables follow the
// OpenType specification's layouts (head 54 bytes, hhea 36, maxp version 0.5,
// post format 2 with Pascal-string names, cmap with one record per subtable);
// the CFF writer encodes every DICT offset as a five-byte integer, so the top
// DICT's size does not depend on the offsets it holds; the Type 1 writer
// encrypts with the constants Type1Font decrypts with.

#include "support/font_builder.hpp"

#include "fonts/type1_font.hpp"

#include <algorithm>
#include <array>

namespace pdfkit::test {

namespace {

void u8(std::string& out, unsigned value) {
  out.push_back(static_cast<char>(value & 0xFFU));
}

void u16(std::string& out, unsigned value) {
  u8(out, value >> 8U);
  u8(out, value);
}

void u32(std::string& out, std::uint32_t value) {
  u16(out, value >> 16U);
  u16(out, value & 0xFFFFU);
}

void pad4(std::string& out) {
  while (out.size() % 4 != 0) {
    out.push_back('\0');
  }
}

std::string cmap_subtable(const FontBuilder::CmapSubtable& table) {
  std::string out;
  switch (table.format) {
    case 0: {
      u16(out, 0);
      u16(out, 262);
      u16(out, 0);
      std::array<unsigned char, 256> glyphs{};
      for (const auto& [code, glyph] : table.map) {
        if (code < 256) {
          glyphs[code] = static_cast<unsigned char>(glyph);
        }
      }
      for (const unsigned char glyph : glyphs) {
        u8(out, glyph);
      }
      break;
    }
    case 4: {
      // One segment per code, with idDelta, then the closing 0xFFFF segment.
      std::vector<std::pair<std::uint32_t, std::uint16_t>> entries(table.map.begin(),
                                                                   table.map.end());
      const std::size_t segments = entries.size() + 1;
      std::string body;
      for (const auto& entry : entries) {
        u16(body, entry.first);
      }
      u16(body, 0xFFFF);
      u16(body, 0);
      for (const auto& entry : entries) {
        u16(body, entry.first);
      }
      u16(body, 0xFFFF);
      for (const auto& entry : entries) {
        u16(body, (entry.second - entry.first) & 0xFFFFU);
      }
      u16(body, 1);
      for (std::size_t i = 0; i < segments; ++i) {
        u16(body, 0);
      }
      u16(out, 4);
      u16(out, static_cast<unsigned>(14 + body.size()));
      u16(out, 0);
      u16(out, static_cast<unsigned>(segments * 2));
      u16(out, 0);
      u16(out, 0);
      u16(out, 0);
      out += body;
      break;
    }
    case 6: {
      const std::uint32_t first = table.map.empty() ? 0 : table.map.begin()->first;
      const std::uint32_t last = table.map.empty() ? 0 : table.map.rbegin()->first;
      const std::uint32_t count = table.map.empty() ? 0 : last - first + 1;
      u16(out, 6);
      u16(out, 10 + count * 2);
      u16(out, 0);
      u16(out, first);
      u16(out, count);
      for (std::uint32_t code = first; code < first + count; ++code) {
        const auto found = table.map.find(code);
        u16(out, found == table.map.end() ? 0 : found->second);
      }
      break;
    }
    case 12: {
      u16(out, 12);
      u16(out, 0);
      u32(out, static_cast<std::uint32_t>(16 + table.map.size() * 12));
      u32(out, 0);
      u32(out, static_cast<std::uint32_t>(table.map.size()));
      for (const auto& [code, glyph] : table.map) {
        u32(out, code);
        u32(out, code);
        u32(out, glyph);
      }
      break;
    }
    default:
      break;
  }
  return out;
}

// A Type 2 or Type 1 charstring integer: 28 and a 16-bit value (Type 2), or
// 255 and a 32-bit value (Type 1).
void type2_number(std::string& out, int value) {
  u8(out, 28);
  u16(out, static_cast<unsigned>(value) & 0xFFFFU);
}

void type1_number(std::string& out, int value) {
  if (value >= -107 && value <= 107) {
    u8(out, static_cast<unsigned>(value + 139));
    return;
  }
  u8(out, 255);
  u32(out, static_cast<std::uint32_t>(value));
}

// A DICT integer in its five-byte form.
void dict_int(std::string& out, std::int32_t value) {
  u8(out, 29);
  u32(out, static_cast<std::uint32_t>(value));
}

std::string index_of(const std::vector<std::string>& items) {
  std::string out;
  u16(out, static_cast<unsigned>(items.size()));
  if (items.empty()) {
    return out;
  }
  u8(out, 4);
  std::uint32_t offset = 1;
  u32(out, offset);
  for (const std::string& item : items) {
    offset += static_cast<std::uint32_t>(item.size());
    u32(out, offset);
  }
  for (const std::string& item : items) {
    out += item;
  }
  return out;
}

std::string bytes_of(const std::vector<std::uint8_t>& data) {
  return {data.begin(), data.end()};
}

std::vector<std::uint8_t> vector_of(const std::string& text) {
  return {text.begin(), text.end()};
}

}  // namespace

std::string FontBuilder::true_type(const TrueType& spec) {
  const std::uint16_t glyphs =
      spec.glyph_count != 0 ? spec.glyph_count : static_cast<std::uint16_t>(spec.advances.size());
  std::vector<std::pair<std::string, std::string>> tables;

  std::string cmap;
  u16(cmap, 0);
  u16(cmap, static_cast<unsigned>(spec.cmaps.size()));
  std::string subtables;
  const std::size_t header = 4 + spec.cmaps.size() * 8;
  for (const CmapSubtable& table : spec.cmaps) {
    u16(cmap, table.platform);
    u16(cmap, table.encoding);
    u32(cmap, static_cast<std::uint32_t>(header + subtables.size()));
    subtables += cmap_subtable(table);
  }
  cmap += subtables;
  tables.emplace_back("cmap", cmap);

  std::string head;
  u32(head, 0x00010000);
  u32(head, 0x00010000);
  u32(head, 0);
  u32(head, 0x5F0F3CF5);
  u16(head, 0);
  u16(head, spec.units_per_em);
  head.append(16, '\0');
  u16(head, 0);
  u16(head, static_cast<unsigned>(static_cast<std::uint16_t>(spec.descender)));
  u16(head, 1000);
  u16(head, static_cast<unsigned>(spec.ascender));
  u16(head, 0);
  u16(head, 8);
  u16(head, 2);
  u16(head, 0);
  u16(head, 0);
  tables.emplace_back("head", head);

  std::string hhea;
  u32(hhea, 0x00010000);
  u16(hhea, static_cast<unsigned>(static_cast<std::uint16_t>(spec.ascender)));
  u16(hhea, static_cast<unsigned>(static_cast<std::uint16_t>(spec.descender)));
  // lineGap to the reserved fields, then metricDataFormat.
  hhea.append(24, '\0');
  u16(hhea, 0);
  u16(hhea, static_cast<unsigned>(spec.advances.size()));
  tables.emplace_back("hhea", hhea);

  std::string hmtx;
  for (const std::uint16_t advance : spec.advances) {
    u16(hmtx, advance);
    u16(hmtx, 0);
  }
  for (std::size_t glyph = spec.advances.size(); glyph < glyphs; ++glyph) {
    u16(hmtx, 0);
  }
  tables.emplace_back("hmtx", hmtx);

  std::string maxp;
  u32(maxp, 0x00005000);
  u16(maxp, glyphs);
  tables.emplace_back("maxp", maxp);

  if (!spec.post_names.empty()) {
    std::string post;
    u32(post, 0x00020000);
    post.append(28, '\0');
    u16(post, static_cast<unsigned>(spec.post_names.size()));
    std::string names;
    std::uint16_t custom = 0;
    for (const std::string& name : spec.post_names) {
      if (name == ".notdef") {
        u16(post, 0);
        continue;
      }
      u16(post, 258U + custom++);
      u8(names, static_cast<unsigned>(name.size()));
      names += name;
    }
    post += names;
    tables.emplace_back("post", post);
  }

  if (!spec.empty_glyphs.empty()) {
    std::string loca;
    std::string glyf;
    for (const bool empty : spec.empty_glyphs) {
      u16(loca, static_cast<unsigned>(glyf.size() / 2));
      if (!empty) {
        glyf.append(12, '\0');
      }
    }
    u16(loca, static_cast<unsigned>(glyf.size() / 2));
    tables.emplace_back("loca", loca);
    tables.emplace_back("glyf", glyf);
  }
  if (!spec.cff.empty()) {
    tables.emplace_back("CFF ", spec.cff);
  }

  std::sort(tables.begin(), tables.end());
  std::string out;
  u32(out, spec.cff.empty() ? 0x00010000U : 0x4F54544FU);
  u16(out, static_cast<unsigned>(tables.size()));
  u16(out, 0);
  u16(out, 0);
  u16(out, 0);
  std::size_t offset = 12 + tables.size() * 16;
  std::string data;
  for (const auto& [tag, bytes] : tables) {
    out += tag;
    u32(out, 0);
    u32(out, static_cast<std::uint32_t>(offset + data.size()));
    u32(out, static_cast<std::uint32_t>(bytes.size()));
    data += bytes;
    pad4(data);
  }
  return out + data;
}

std::string FontBuilder::cff(const Cff& spec) {
  const bool cid = !spec.cids.empty();
  const std::size_t glyphs = cid ? spec.cids.size() : spec.glyph_names.size();
  // The strings: custom glyph names past the standard ones, then the ROS.
  std::vector<std::string> strings;
  const auto sid_of = [&strings](const std::string& name) -> unsigned {
    // The standard strings this writer uses.
    static const std::map<std::string, unsigned> standard = {
        {".notdef", 0}, {"space", 1}, {"A", 34}, {"B", 35},   {"C", 36},
        {"a", 66},      {"b", 67},    {"c", 68}, {"fi", 109}, {"fl", 110}};
    if (const auto found = standard.find(name); found != standard.end()) {
      return found->second;
    }
    const auto found = std::find(strings.begin(), strings.end(), name);
    if (found != strings.end()) {
      return 391U + static_cast<unsigned>(found - strings.begin());
    }
    strings.push_back(name);
    return 391U + static_cast<unsigned>(strings.size() - 1);
  };

  std::string charset;
  u8(charset, static_cast<unsigned>(spec.charset_format));
  std::vector<unsigned> sids;
  for (std::size_t glyph = 1; glyph < glyphs; ++glyph) {
    sids.push_back(cid ? spec.cids[glyph] : sid_of(spec.glyph_names[glyph]));
  }
  if (spec.charset_format == 0) {
    for (const unsigned sid : sids) {
      u16(charset, sid);
    }
  } else {
    // One range per SID, so any order is expressible.
    for (const unsigned sid : sids) {
      u16(charset, sid);
      if (spec.charset_format == 1) {
        u8(charset, 0);
      } else {
        u16(charset, 0);
      }
    }
  }
  unsigned registry = 0;
  unsigned ordering = 0;
  if (cid) {
    registry = sid_of("Adobe");
    ordering = sid_of("Identity");
  }

  std::string encoding;
  if (!spec.encoding.empty()) {
    u8(encoding, 0);
    // Format 0 lists the code of glyphs 1, 2, ... in order.
    std::map<std::uint16_t, std::uint8_t> by_glyph;
    for (const auto& [code, glyph] : spec.encoding) {
      by_glyph[glyph] = code;
    }
    const std::uint16_t last = by_glyph.empty() ? 0 : by_glyph.rbegin()->first;
    u8(encoding, last);
    for (std::uint16_t glyph = 1; glyph <= last; ++glyph) {
      const auto found = by_glyph.find(glyph);
      u8(encoding, found == by_glyph.end() ? 0 : found->second);
    }
  }

  std::vector<std::string> charstrings;
  for (std::size_t glyph = 0; glyph < glyphs; ++glyph) {
    std::string code;
    const std::optional<int> width = glyph < spec.widths.size() ? spec.widths[glyph] : std::nullopt;
    if (glyph == 1 && spec.width_in_subroutine && width) {
      // The local subroutine 0 (biased index -107) pushes the width.
      type2_number(code, -107);
      u8(code, 10);
      u8(code, 14);
    } else {
      if (width) {
        type2_number(code, *width);
      }
      u8(code, 14);
    }
    charstrings.push_back(code);
  }
  std::string subrs_index;
  if (spec.width_in_subroutine && spec.widths.size() > 1 && spec.widths[1]) {
    std::string subr;
    type2_number(subr, *spec.widths[1]);
    u8(subr, 11);
    subrs_index = index_of({subr});
  }

  std::string priv;
  dict_int(priv, spec.default_width);
  u8(priv, 20);
  dict_int(priv, spec.nominal_width);
  u8(priv, 21);
  if (!subrs_index.empty()) {
    dict_int(priv, static_cast<std::int32_t>(priv.size() + 6));
    u8(priv, 19);
  }

  // The top DICT, its size fixed by the five-byte integers.
  const auto top_dict = [&](std::uint32_t charset_at, std::uint32_t encoding_at,
                            std::uint32_t charstrings_at, std::uint32_t private_at,
                            std::uint32_t fd_array_at, std::uint32_t fd_select_at) {
    std::string dict;
    if (cid) {
      dict_int(dict, static_cast<std::int32_t>(registry));
      dict_int(dict, static_cast<std::int32_t>(ordering));
      dict_int(dict, 0);
      u8(dict, 12);
      u8(dict, 30);
    }
    dict_int(dict, static_cast<std::int32_t>(charset_at));
    u8(dict, 15);
    if (!cid) {
      dict_int(dict, static_cast<std::int32_t>(encoding_at));
      u8(dict, 16);
    }
    dict_int(dict, static_cast<std::int32_t>(charstrings_at));
    u8(dict, 17);
    if (cid) {
      dict_int(dict, static_cast<std::int32_t>(fd_array_at));
      u8(dict, 12);
      u8(dict, 36);
      dict_int(dict, static_cast<std::int32_t>(fd_select_at));
      u8(dict, 12);
      u8(dict, 37);
    } else {
      dict_int(dict, static_cast<std::int32_t>(priv.size()));
      dict_int(dict, static_cast<std::int32_t>(private_at));
      u8(dict, 18);
    }
    return dict;
  };

  std::string header;
  u8(header, 1);
  u8(header, 0);
  u8(header, 4);
  u8(header, 4);
  const std::string names = index_of({"Test"});
  const std::size_t top_size = index_of({top_dict(0, 0, 0, 0, 0, 0)}).size();
  const std::string string_index = index_of(strings);
  const std::string global_subrs = index_of({});
  std::size_t at =
      header.size() + names.size() + top_size + string_index.size() + global_subrs.size();
  const std::size_t charset_at = at;
  at += charset.size();
  const std::size_t encoding_at = encoding.empty() ? 0 : at;
  at += encoding.size();
  const std::string charstring_index = index_of(charstrings);
  const std::size_t charstrings_at = at;
  at += charstring_index.size();
  const std::size_t private_at = at;
  at += priv.size() + subrs_index.size();
  std::string fd_array;
  std::string fd_select;
  std::size_t fd_array_at = 0;
  std::size_t fd_select_at = 0;
  if (cid) {
    std::string fd;
    dict_int(fd, static_cast<std::int32_t>(priv.size()));
    dict_int(fd, static_cast<std::int32_t>(private_at));
    u8(fd, 18);
    fd_array = index_of({fd});
    fd_array_at = at;
    at += fd_array.size();
    u8(fd_select, 3);
    u16(fd_select, 1);
    u16(fd_select, 0);
    u8(fd_select, 0);
    u16(fd_select, static_cast<unsigned>(glyphs));
    fd_select_at = at;
  }
  const std::string top = index_of({top_dict(
      static_cast<std::uint32_t>(charset_at), static_cast<std::uint32_t>(encoding_at),
      static_cast<std::uint32_t>(charstrings_at), static_cast<std::uint32_t>(private_at),
      static_cast<std::uint32_t>(fd_array_at), static_cast<std::uint32_t>(fd_select_at))});
  return header + names + top + string_index + global_subrs + charset + encoding +
         charstring_index + priv + subrs_index + fd_array + fd_select;
}

std::string FontBuilder::type1(const Type1& spec) {
  std::string clear = "%!PS-AdobeFont-1.0: " + spec.font_name + " 001.000\n";
  clear += "11 dict begin\n/FontName /" + spec.font_name + " def\n";
  clear += "/FontMatrix [0.001 0 0 0.001 0 0] readonly def\n";
  clear += "/FontBBox {-50 -250 1000 900} readonly def\n";
  if (spec.encoding.empty()) {
    clear += "/Encoding StandardEncoding def\n";
  } else {
    clear += "/Encoding 256 array\n0 1 255 {1 index exch /.notdef put} for\n";
    for (const auto& [code, name] : spec.encoding) {
      clear += "dup " + std::to_string(code) + " /" + name + " put\n";
    }
    clear += "readonly def\n";
  }
  clear += "currentdict end\ncurrentfile eexec\n";

  std::string priv =
      "dup /Private 8 dict dup begin\n/RD{string currentfile exch readstring pop}"
      "executeonly def\n/ND{noaccess def}executeonly def\n/lenIV 4 def\n"
      "/Subrs 0 array\n2 index /CharStrings " +
      std::to_string(spec.glyphs.size()) + " dict dup begin\n";
  for (const auto& [name, width] : spec.glyphs) {
    std::string code = "\x01\x02\x03\x04";
    type1_number(code, 0);
    if (spec.sbw) {
      type1_number(code, 0);
      type1_number(code, width);
      type1_number(code, 0);
      u8(code, 12);
      u8(code, 7);
    } else {
      type1_number(code, width);
      u8(code, 13);
    }
    u8(code, 14);
    const std::string encrypted = bytes_of(fonts::Type1Font::encrypt(vector_of(code), 4330));
    priv += "/" + name + " " + std::to_string(encrypted.size()) + " RD " + encrypted + " ND\n";
  }
  priv +=
      "end\nend\nreadonly put\nnoaccess put\ndup/FontName get exch definefont pop\n"
      "mark currentfile closefile\n";
  std::string cipher =
      bytes_of(fonts::Type1Font::encrypt(vector_of("\x11\x22\x33\x44" + priv), 55665));
  if (spec.truncate_eexec && *spec.truncate_eexec < cipher.size()) {
    cipher.resize(*spec.truncate_eexec);
  }
  if (spec.hex_eexec) {
    static const char* const digits = "0123456789abcdef";
    std::string hex;
    for (std::size_t i = 0; i < cipher.size(); ++i) {
      const auto value = static_cast<unsigned char>(cipher[i]);
      hex.push_back(digits[value >> 4U]);
      hex.push_back(digits[value & 0x0FU]);
      if (i % 32 == 31) {
        hex.push_back('\n');
      }
    }
    cipher = hex;
  }
  const std::string trailer = std::string(512, '0') + "\ncleartomark\n";
  if (!spec.pfb) {
    return clear + cipher + trailer;
  }
  const auto segment = [](unsigned type, const std::string& data) {
    std::string out;
    u8(out, 0x80);
    u8(out, type);
    const auto size = static_cast<std::uint32_t>(data.size());
    u8(out, size);
    u8(out, size >> 8U);
    u8(out, size >> 16U);
    u8(out, size >> 24U);
    return out + data;
  };
  std::string end;
  u8(end, 0x80);
  u8(end, 3);
  return segment(1, clear) + segment(2, cipher) + segment(1, trailer) + end;
}

}  // namespace pdfkit::test
