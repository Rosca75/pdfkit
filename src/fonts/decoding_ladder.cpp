// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Each function walks its rungs top to bottom and returns at the first that
// answers with usable text. The program rung of a TrueType glyph prefers the
// program's Unicode cmap over its post name, because a cmap is what the
// program itself declares its glyph to be.

#include "fonts/decoding_ladder.hpp"

#include "fonts/cmap.hpp"
#include "fonts/font_program.hpp"
#include "fonts/glyph_list.hpp"
#include "fonts/ligatures.hpp"
#include "fonts/simple_font.hpp"
#include "fonts/type0_font.hpp"
#include "platform/utf8_sequence.hpp"

namespace pdfkit::fonts {

namespace {

// The text of a TrueType or OpenType glyph: the program's Unicode cmap, then
// its post name through the glyph list.
std::optional<std::string> true_type_text(const TrueTypeTables& tables, std::uint32_t glyph) {
  if (const std::optional<char32_t> code_point = tables.unicode_of(glyph);
      code_point && GlyphList::usable(*code_point)) {
    std::string text;
    platform::append_utf8(text, *code_point);
    return text;
  }
  const std::string_view name = tables.glyph_name(glyph);
  if (!name.empty()) {
    return GlyphList::to_unicode(name);
  }
  return std::nullopt;
}

// The text a program gives a glyph-number name ("g12", "C65").
std::optional<std::string> numbered(const SimpleFont& font, const GlyphList::Number& number) {
  const FontProgram* program = font.font_program();
  if (program == nullptr) {
    return std::nullopt;
  }
  if (number.kind == GlyphList::Number::Kind::glyph) {
    if (const TrueTypeTables* tables = program->true_type()) {
      return true_type_text(*tables, number.value);
    }
    if (const CffFont* cff = program->cff(); cff != nullptr && !cff->is_cid()) {
      const std::string_view name = cff->glyph_name(number.value);
      return name.empty() ? std::nullopt : GlyphList::to_unicode(name);
    }
    return std::nullopt;
  }
  if (number.kind == GlyphList::Number::Kind::code && number.value < 256) {
    if (program->true_type() != nullptr) {
      if (const std::optional<std::uint32_t> glyph = font.true_type_glyph(number.value)) {
        return true_type_text(*program->true_type(), *glyph);
      }
    }
    const std::string_view name = program->builtin_name(static_cast<std::uint8_t>(number.value));
    if (!name.empty()) {
      return GlyphList::to_unicode(name);
    }
  }
  return std::nullopt;
}

// The UTF-8 text of a code of a predefined Unicode CMap.
std::optional<std::string> unicode_code(Type0Font::UnicodeCodes codes, std::uint32_t code) {
  std::string text;
  switch (codes) {
    case Type0Font::UnicodeCodes::none:
      return std::nullopt;
    case Type0Font::UnicodeCodes::ucs2:
      platform::append_utf8(text, static_cast<char32_t>(code & 0xFFFFU));
      break;
    case Type0Font::UnicodeCodes::utf16:
      if (code > 0xFFFF) {
        const std::uint32_t high = code >> 16U;
        const std::uint32_t low = code & 0xFFFFU;
        if (high < 0xD800 || high > 0xDBFF || low < 0xDC00 || low > 0xDFFF) {
          return std::nullopt;
        }
        platform::append_utf8(
            text, static_cast<char32_t>(0x10000 + ((high - 0xD800) << 10U) + (low - 0xDC00)));
      } else {
        platform::append_utf8(text, static_cast<char32_t>(code));
      }
      break;
    case Type0Font::UnicodeCodes::utf8: {
      std::string bytes;
      for (int shift = 24; shift >= 0; shift -= 8) {
        const auto value = static_cast<char>((code >> static_cast<unsigned>(shift)) & 0xFFU);
        if (!bytes.empty() || value != 0 || shift == 0) {
          bytes.push_back(value);
        }
      }
      const platform::Utf8Sequence sequence = platform::next_utf8_sequence(bytes, 0);
      if (!sequence.valid || sequence.length != bytes.size()) {
        return std::nullopt;
      }
      text = bytes;
      break;
    }
    case Type0Font::UnicodeCodes::utf32:
      if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        return std::nullopt;
      }
      platform::append_utf8(text, static_cast<char32_t>(code));
      break;
  }
  return text;
}

}  // namespace

bool DecodingLadder::usable_text(std::string_view text) noexcept {
  if (text.empty()) {
    return false;
  }
  std::size_t at = 0;
  while (at < text.size()) {
    const platform::Utf8Sequence sequence = platform::next_utf8_sequence(text, at);
    if (!sequence.valid || !GlyphList::usable(sequence.code_point)) {
      return false;
    }
    at += sequence.length;
  }
  return true;
}

std::optional<std::string> DecodingLadder::from_to_unicode(const Cmap* map, std::uint32_t code) {
  if (map == nullptr) {
    return std::nullopt;
  }
  std::optional<std::string> text = map->unicode(code);
  if (!text || !usable_text(*text)) {
    return std::nullopt;
  }
  return text;
}

PdfFont::Answer DecodingLadder::answer(std::string text, DecodingRung rung) {
  PdfFont::Answer out;
  out.rung = rung;
  if (std::optional<std::string> letters = Ligatures::decompose(text)) {
    out.text = std::move(*letters);
    out.decomposed = true;
  } else {
    out.text = std::move(text);
  }
  return out;
}

PdfFont::Answer DecodingLadder::simple(const SimpleFont& font, std::uint32_t code) {
  if (std::optional<std::string> text = from_to_unicode(font.to_unicode(), code)) {
    return answer(std::move(*text), DecodingRung::to_unicode);
  }
  if (code > 0xFF) {
    return {};
  }
  const auto byte = static_cast<std::uint8_t>(code);
  const std::string_view name = font.glyph_name(byte);
  const SimpleFont::NameSource source = font.name_source(byte);
  const bool type3 = font.kind() == PdfFont::Kind::type3;
  if (!name.empty() && source != SimpleFont::NameSource::none) {
    if (std::optional<std::string> text = GlyphList::to_unicode(name)) {
      return answer(std::move(*text), source == SimpleFont::NameSource::program
                                          ? DecodingRung::font_program
                                          : DecodingRung::glyph_name);
    }
  }
  if (type3) {
    return {};
  }
  if (!name.empty()) {
    if (const std::optional<GlyphList::Number> number = GlyphList::number(name)) {
      if (std::optional<std::string> text = numbered(font, *number)) {
        if (usable_text(*text)) {
          return answer(std::move(*text), DecodingRung::font_program);
        }
      }
    }
  }
  const FontProgram* program = font.font_program();
  if (program == nullptr) {
    return {};
  }
  if (const TrueTypeTables* tables = program->true_type()) {
    if (const std::optional<std::uint32_t> glyph = font.true_type_glyph(code)) {
      if (std::optional<std::string> text = true_type_text(*tables, *glyph);
          text && usable_text(*text)) {
        return answer(std::move(*text), DecodingRung::font_program);
      }
    }
  }
  const std::string_view builtin = program->builtin_name(byte);
  if (!builtin.empty()) {
    if (std::optional<std::string> text = GlyphList::to_unicode(builtin)) {
      return answer(std::move(*text), DecodingRung::font_program);
    }
  }
  return {};
}

PdfFont::Answer DecodingLadder::composite(const Type0Font& font, std::uint32_t code) {
  if (std::optional<std::string> text = from_to_unicode(font.to_unicode(), code)) {
    return answer(std::move(*text), DecodingRung::to_unicode);
  }
  if (std::optional<std::string> text = unicode_code(font.codes_are_unicode(), code);
      text && usable_text(*text)) {
    return answer(std::move(*text), DecodingRung::glyph_name);
  }
  if (!font.predefined().empty()) {
    // A predefined CMap without data: no CID, so no glyph to name.
    return {};
  }
  const std::optional<std::uint32_t> cid = font.encoding().cid(code);
  const FontProgram* program = font.font_program();
  if (!cid || program == nullptr) {
    return {};
  }
  const std::optional<std::uint32_t> glyph = font.glyph_for_cid(*cid);
  if (!glyph) {
    return {};
  }
  const CffFont* cff = program->cff();
  const TrueTypeTables* tables = program->true_type();
  if (cff != nullptr && !cff->is_cid() && !font.true_type_glyphs()) {
    const std::string_view name = cff->glyph_name(*glyph);
    if (!name.empty()) {
      if (std::optional<std::string> text = GlyphList::to_unicode(name)) {
        return answer(std::move(*text), DecodingRung::glyph_name);
      }
    }
  }
  if (tables != nullptr) {
    const std::string_view name = tables->glyph_name(*glyph);
    if (!name.empty()) {
      if (std::optional<std::string> text = GlyphList::to_unicode(name)) {
        return answer(std::move(*text), DecodingRung::glyph_name);
      }
    }
    if (const std::optional<char32_t> code_point = tables->unicode_of(*glyph);
        code_point && GlyphList::usable(*code_point)) {
      std::string text;
      platform::append_utf8(text, *code_point);
      return answer(std::move(text), DecodingRung::font_program);
    }
  }
  return {};
}

}  // namespace pdfkit::fonts
