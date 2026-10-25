// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// A name is matched by family and style: the family is what precedes the
// first ',' or '-' with spaces removed, the style is read from the rest
// ("Bold", "Italic", "Oblique" and their combinations; "MT" and "PS" are
// producer tags). The families are the Core 14 names and the aliases of pdf.js
// getStdFontMap (read): Arial, ArialMT, ArialNarrow, ArialBlack,
// ArialUnicodeMS and HelveticaLTStd as Helvetica; TimesNewRoman,
// TimesNewRomanPS and TimesNewRomanPSMT as Times; CourierNew, CourierNewPS and
// CourierNewPSMT as Courier. A glyph is found by binary search in the face's
// slice of the generated table.

#include "fonts/standard_fonts.hpp"

#include "fonts/encodings.hpp"
#include "fonts/generated/core14_table.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>

namespace pdfkit::fonts {

namespace {

using generated::Core14Table;

enum class Family : std::uint8_t { times, helvetica, courier, symbol, zapf_dingbats };

struct Alias {
  std::string_view family;
  Family target;
};

constexpr std::array<Alias, 20> families = {{
    {"Times", Family::times},
    {"TimesRoman", Family::times},
    {"TimesNewRoman", Family::times},
    {"TimesNewRomanPS", Family::times},
    {"TimesNewRomanPSMT", Family::times},
    {"Helvetica", Family::helvetica},
    {"HelveticaLTStd", Family::helvetica},
    {"Arial", Family::helvetica},
    {"ArialMT", Family::helvetica},
    {"ArialNarrow", Family::helvetica},
    {"ArialBlack", Family::helvetica},
    {"ArialUnicodeMS", Family::helvetica},
    {"Courier", Family::courier},
    {"CourierNew", Family::courier},
    {"CourierNewPS", Family::courier},
    {"CourierNewPSMT", Family::courier},
    {"Symbol", Family::symbol},
    {"SymbolMT", Family::symbol},
    {"ZapfDingbats", Family::zapf_dingbats},
    {"ZapfDingbatsITC", Family::zapf_dingbats},
}};

bool contains(std::string_view text, std::string_view word) noexcept {
  return text.find(word) != std::string_view::npos;
}

std::span<const Core14Table::Glyph> glyphs_of(StandardFonts::Face face) noexcept {
  const Core14Table::Font& font = Core14Table::fonts()[static_cast<std::size_t>(face)];
  return Core14Table::glyphs().subspan(font.first, font.count);
}

}  // namespace

std::string_view StandardFonts::strip_subset(std::string_view name) noexcept {
  if (name.size() > 7 && name[6] == '+' &&
      std::all_of(name.begin(), name.begin() + 6, [](char c) { return c >= 'A' && c <= 'Z'; })) {
    return name.substr(7);
  }
  return name;
}

std::optional<StandardFonts::Face> StandardFonts::find(std::string_view base_font) noexcept {
  const std::string_view stripped = strip_subset(base_font);
  for (std::size_t i = 0; i < Core14Table::fonts().size(); ++i) {
    if (Core14Table::fonts()[i].name == stripped) {
      return static_cast<Face>(i);
    }
  }
  const std::size_t split = stripped.find_first_of(",-");
  std::string family;
  for (const char c : stripped.substr(0, split)) {
    if (c != ' ') {
      family.push_back(c);
    }
  }
  const std::string_view style =
      split == std::string_view::npos ? std::string_view() : stripped.substr(split + 1);
  const auto* alias = std::ranges::find(families, std::string_view(family), &Alias::family);
  if (alias == families.end()) {
    return std::nullopt;
  }
  const bool bold = contains(style, "Bold") || contains(style, "Black") ||
                    contains(style, "Heavy") || contains(family, "Black");
  const bool italic = contains(style, "Italic") || contains(style, "Oblique");
  switch (alias->target) {
    case Family::times:
      return bold ? (italic ? Face::times_bold_italic : Face::times_bold)
                  : (italic ? Face::times_italic : Face::times_roman);
    case Family::helvetica:
      return bold ? (italic ? Face::helvetica_bold_oblique : Face::helvetica_bold)
                  : (italic ? Face::helvetica_oblique : Face::helvetica);
    case Family::courier:
      return bold ? (italic ? Face::courier_bold_oblique : Face::courier_bold)
                  : (italic ? Face::courier_oblique : Face::courier);
    case Family::symbol:
      return Face::symbol;
    case Family::zapf_dingbats:
      return Face::zapf_dingbats;
  }
  return std::nullopt;
}

std::string_view StandardFonts::name(Face face) noexcept {
  return Core14Table::fonts()[static_cast<std::size_t>(face)].name;
}

std::optional<int> StandardFonts::width(Face face, std::string_view glyph) noexcept {
  const std::span<const Core14Table::Glyph> glyphs = glyphs_of(face);
  const auto found = std::ranges::lower_bound(glyphs, glyph, {}, &Core14Table::Glyph::name);
  if (found == glyphs.end() || found->name != glyph) {
    return std::nullopt;
  }
  return found->width;
}

std::string_view StandardFonts::encoded_name(Face face, std::uint8_t code) noexcept {
  if (!is_symbolic(face)) {
    return Encodings::name(Encodings::Base::standard, code);
  }
  return Encodings::name(
      face == Face::symbol ? Encodings::Base::symbol : Encodings::Base::zapf_dingbats, code);
}

bool StandardFonts::is_bold(Face face) noexcept {
  return face == Face::times_bold || face == Face::times_bold_italic ||
         face == Face::helvetica_bold || face == Face::helvetica_bold_oblique ||
         face == Face::courier_bold || face == Face::courier_bold_oblique;
}

int StandardFonts::ascender(Face face) noexcept {
  return Core14Table::fonts()[static_cast<std::size_t>(face)].ascender;
}

int StandardFonts::descender(Face face) noexcept {
  return Core14Table::fonts()[static_cast<std::size_t>(face)].descender;
}

}  // namespace pdfkit::fonts
