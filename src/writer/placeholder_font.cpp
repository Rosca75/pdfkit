// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The compiled subsets and the objects that embed one. The descriptor's
// /Flags is 32 (nonsymbolic: the glyphs are of the standard Latin set, ISO
// 32000-1 section 9.8.2, read); the bold face states /FontWeight 700 and a
// name ending in "-Bold", which is how the library's own reader (and every
// viewer) tells it is bold. The CIDs run from 1 in the order the subset
// holds its glyphs, so /W is one run of widths.

#include "writer/placeholder_font.hpp"

#include "platform/byte_span.hpp"
#include "platform/error.hpp"
#include "syntax/dictionary.hpp"
#include "writer/stream_copier.hpp"

#include <algorithm>
#include <vector>

namespace pdfkit::writer {

namespace {

struct SubsetGlyph {
  char character;
  std::uint16_t gid;
  std::uint16_t width;
};

struct SubsetFace {
  std::span<const std::uint8_t> program;
  std::span<const SubsetGlyph> glyphs;
  std::string_view sha256;
  std::string_view base_font;
  int notdef_width;
  int ascent;
  int descent;
  int cap_height;
  std::array<int, 4> bbox;
  int stem_v;
  int weight;
  int glyph_count;
};

#include "writer/placeholder_font_data.inc"

using syntax::Dictionary;
using syntax::Object;

[[nodiscard]] const SubsetFace& face_data(PlaceholderFont::Face face) noexcept {
  return face == PlaceholderFont::Face::bold ? bold_face : regular_face;
}

[[nodiscard]] const SubsetGlyph* glyph(PlaceholderFont::Face face, char character) noexcept {
  for (const SubsetGlyph& entry : face_data(face).glyphs) {
    if (entry.character == character) {
      return &entry;
    }
  }
  return nullptr;
}

[[nodiscard]] std::string hex4(unsigned value) {
  constexpr std::string_view digits = "0123456789ABCDEF";
  std::string out;
  for (int shift = 12; shift >= 0; shift -= 4) {
    out += digits[(value >> static_cast<unsigned>(shift)) & 0x0FU];
  }
  return out;
}

[[nodiscard]] Object integers(std::initializer_list<int> values) {
  Object::Array items;
  for (const int value : values) {
    items.push_back(Object::make_integer(value));
  }
  return Object::make_array(std::move(items));
}

[[nodiscard]] platform::ByteSpan bytes_of(std::span<const std::uint8_t> data) noexcept {
  return {reinterpret_cast<const std::byte*>(data.data()), data.size()};
}

}  // namespace

bool PlaceholderFont::in_alphabet(std::string_view text) noexcept {
  return !outside(text).has_value();
}

std::optional<char> PlaceholderFont::outside(std::string_view text) noexcept {
  for (const char value : text) {
    if (alphabet.find(value) == std::string_view::npos) {
      return value;
    }
  }
  return std::nullopt;
}

std::string PlaceholderFont::codes(std::string_view text, Face face) {
  std::string out;
  out.reserve(text.size() * 2);
  for (const char value : text) {
    const SubsetGlyph* entry = glyph(face, value);
    if (entry == nullptr) {
      throw platform::Error(platform::ErrorCode::internal,
                            "the placeholder font has no glyph for a character it was given",
                            "a character of the placeholder alphabet",
                            "report this defect; placeholders are checked before writing");
    }
    out.push_back(static_cast<char>(entry->gid >> 8U));
    out.push_back(static_cast<char>(entry->gid & 0xFFU));
  }
  return out;
}

double PlaceholderFont::width(std::string_view text, Face face) {
  double total = 0.0;
  for (const char value : text) {
    const SubsetGlyph* entry = glyph(face, value);
    total += entry != nullptr ? entry->width : face_data(face).notdef_width;
  }
  return total;
}

const PlaceholderFont::Metrics& PlaceholderFont::metrics(Face face) {
  static const std::array<Metrics, 2> table = [] {
    std::array<Metrics, 2> out{};
    for (const Face each : {Face::regular, Face::bold}) {
      const SubsetFace& data = face_data(each);
      out[static_cast<std::size_t>(each)] = {
          data.base_font, data.sha256,     data.notdef_width, data.ascent, data.descent,
          data.cap_height, data.bbox, data.stem_v,       data.weight, data.program};
    }
    return out;
  }();
  return table[static_cast<std::size_t>(face)];
}

std::string PlaceholderFont::resource_name(Face face) {
  return "PdfkitPH" + std::string(face_data(face).sha256.substr(0, 8));
}

std::string PlaceholderFont::to_unicode(Face face) {
  std::vector<const SubsetGlyph*> sorted;
  for (const SubsetGlyph& entry : face_data(face).glyphs) {
    sorted.push_back(&entry);
  }
  std::ranges::sort(sorted, {}, &SubsetGlyph::gid);
  std::string out =
      "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
      "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
      "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
      "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
  out += std::to_string(sorted.size()) + " beginbfchar\n";
  for (const SubsetGlyph* entry : sorted) {
    out += "<" + hex4(entry->gid) + "> <" + hex4(static_cast<unsigned char>(entry->character)) +
           ">\n";
  }
  out += "endbfchar\nendcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
  return out;
}

bool PlaceholderFont::embedded(Face face) const noexcept {
  return embedded_[static_cast<std::size_t>(face)].has_value();
}

Object::Reference PlaceholderFont::embed(Face face) {
  std::optional<Object::Reference>& slot = embedded_[static_cast<std::size_t>(face)];
  if (slot) {
    return *slot;
  }
  const SubsetFace& data = face_data(face);
  const std::string base(data.base_font);

  Dictionary program;
  program.set("Length1", Object::make_integer(static_cast<std::int64_t>(data.program.size())));
  const Object::Reference file = graph_->add(
      Object::make_stream(StreamCopier::compress(std::move(program), bytes_of(data.program))));

  Dictionary descriptor;
  descriptor.set("Type", Object::make_name("FontDescriptor"));
  descriptor.set("FontName", Object::make_name(base));
  descriptor.set("Flags", Object::make_integer(32));
  descriptor.set("FontBBox", integers({data.bbox[0], data.bbox[1], data.bbox[2], data.bbox[3]}));
  descriptor.set("ItalicAngle", Object::make_integer(0));
  descriptor.set("Ascent", Object::make_integer(data.ascent));
  descriptor.set("Descent", Object::make_integer(data.descent));
  descriptor.set("CapHeight", Object::make_integer(data.cap_height));
  descriptor.set("StemV", Object::make_integer(data.stem_v));
  descriptor.set("FontWeight", Object::make_integer(data.weight));
  descriptor.set("FontFile2", Object::make_reference(file));
  const Object::Reference descriptor_reference =
      graph_->add(Object::make_dictionary(std::move(descriptor)));

  std::vector<const SubsetGlyph*> sorted;
  for (const SubsetGlyph& entry : data.glyphs) {
    sorted.push_back(&entry);
  }
  std::ranges::sort(sorted, {}, &SubsetGlyph::gid);
  Object::Array widths;
  for (const SubsetGlyph* entry : sorted) {
    widths.push_back(Object::make_integer(entry->width));
  }
  Dictionary system;
  system.set("Registry", Object::make_string("Adobe"));
  system.set("Ordering", Object::make_string("Identity"));
  system.set("Supplement", Object::make_integer(0));
  Dictionary descendant;
  descendant.set("Type", Object::make_name("Font"));
  descendant.set("Subtype", Object::make_name("CIDFontType2"));
  descendant.set("BaseFont", Object::make_name(base));
  descendant.set("CIDSystemInfo", Object::make_dictionary(std::move(system)));
  descendant.set("FontDescriptor", Object::make_reference(descriptor_reference));
  descendant.set("DW", Object::make_integer(data.notdef_width));
  descendant.set("W", Object::make_array({Object::make_integer(sorted.empty() ? 1 : sorted.front()->gid),
                                          Object::make_array(std::move(widths))}));
  descendant.set("CIDToGIDMap", Object::make_name("Identity"));
  const Object::Reference descendant_reference =
      graph_->add(Object::make_dictionary(std::move(descendant)));

  const std::string cmap = to_unicode(face);
  const Object::Reference unicode = graph_->add(
      Object::make_stream(StreamCopier::compress(Dictionary(), platform::as_bytes(cmap))));

  Dictionary type0;
  type0.set("Type", Object::make_name("Font"));
  type0.set("Subtype", Object::make_name("Type0"));
  type0.set("BaseFont", Object::make_name(base));
  type0.set("Encoding", Object::make_name("Identity-H"));
  type0.set("DescendantFonts", Object::make_array({Object::make_reference(descendant_reference)}));
  type0.set("ToUnicode", Object::make_reference(unicode));
  slot = graph_->add(Object::make_dictionary(std::move(type0)));
  return *slot;
}

}  // namespace pdfkit::writer
