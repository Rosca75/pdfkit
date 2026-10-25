// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The descendant CIDFont is read once in the constructor. /W and /W2 are kept
// as ranges, not expanded, so a range over every CID costs one entry; an
// entry of the wrong type ends the array's reading, as pdf.js extractWidths
// stops at invalid /W data (read).

#include "fonts/type0_font.hpp"

#include "fonts/decoding_ladder.hpp"

#include <algorithm>
#include <cmath>

namespace pdfkit::fonts {

namespace {

constexpr std::uint32_t flag_symbolic = 4;
constexpr std::uint32_t flag_force_bold = 262144;

std::optional<double> finite(const syntax::Object& value) {
  const std::optional<double> number = value.number();
  if (!number || !std::isfinite(*number)) {
    return std::nullopt;
  }
  return number;
}

std::optional<std::uint32_t> cid_of(const syntax::Object& value) {
  const std::optional<double> number = finite(value);
  if (!number || *number < 0 || *number > 4294967295.0) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(*number);
}

bool contains(std::string_view text, std::string_view word) noexcept {
  return text.find(word) != std::string_view::npos;
}

}  // namespace

Cmap Type0Font::predefined_codespace(std::string_view name) {
  Cmap map;
  switch (unicode_codes(name)) {
    case UnicodeCodes::ucs2:
      map.add_codespace(0x0000, 0xFFFF, 2);
      return map;
    case UnicodeCodes::utf16:
      map.add_codespace(0x0000, 0xD7FF, 2);
      map.add_codespace(0xE000, 0xFFFF, 2);
      map.add_codespace(0xD800DC00, 0xDBFFDFFF, 4);
      return map;
    case UnicodeCodes::utf8:
      map.add_codespace(0x00, 0x7F, 1);
      map.add_codespace(0xC080, 0xDFBF, 2);
      map.add_codespace(0xE08080, 0xEFBFBF, 3);
      map.add_codespace(0xF0808080, 0xF7BFBFBF, 4);
      return map;
    case UnicodeCodes::utf32:
      map.add_codespace(0x00000000, 0x0010FFFF, 4);
      return map;
    case UnicodeCodes::none:
      break;
  }
  if (contains(name, "RKSJ")) {
    // Shift-JIS: single bytes, and lead bytes 0x81 to 0x9F and 0xE0 to 0xFC.
    map.add_codespace(0x00, 0x80, 1);
    map.add_codespace(0xA0, 0xDF, 1);
    map.add_codespace(0xFD, 0xFF, 1);
    map.add_codespace(0x8140, 0x9FFC, 2);
    map.add_codespace(0xE040, 0xFCFC, 2);
    return map;
  }
  if (contains(name, "EUC") || contains(name, "B5") || contains(name, "UHC") ||
      contains(name, "GBK") || contains(name, "HKscs") || contains(name, "ETen")) {
    // EUC, GBK, Big5 and UHC: single bytes up to 0x80, then two-byte codes
    // with a lead byte from 0x81.
    map.add_codespace(0x00, 0x80, 1);
    map.add_codespace(0x8140, 0xFEFE, 2);
    return map;
  }
  map.add_codespace(0x0000, 0xFFFF, 2);
  return map;
}

Type0Font::UnicodeCodes Type0Font::unicode_codes(std::string_view name) noexcept {
  if (!name.starts_with("Uni")) {
    return UnicodeCodes::none;
  }
  if (contains(name, "-UCS2-")) {
    return UnicodeCodes::ucs2;
  }
  if (contains(name, "-UTF16-")) {
    return UnicodeCodes::utf16;
  }
  if (contains(name, "-UTF8-")) {
    return UnicodeCodes::utf8;
  }
  if (contains(name, "-UTF32-")) {
    return UnicodeCodes::utf32;
  }
  return UnicodeCodes::none;
}

Type0Font::Type0Font(syntax::Document& document, const syntax::Dictionary& font,
                     std::uint32_t object_number, std::shared_ptr<const Cmap> encoding,
                     std::string predefined, std::shared_ptr<const FontProgram> program,
                     std::shared_ptr<const std::vector<std::uint16_t>> cid_to_gid,
                     std::shared_ptr<const Cmap> to_unicode, const std::vector<std::string>& faults)
    : encoding_(std::move(encoding)),
      predefined_(std::move(predefined)),
      file_(std::move(program)),
      cid_to_gid_(std::move(cid_to_gid)),
      to_unicode_(std::move(to_unicode)) {
  kind_ = Kind::type0;
  object_number_ = object_number;
  unicode_codes_ = unicode_codes(predefined_);
  vertical_ = encoding_->vertical();
  for (const std::string& note : faults) {
    fault(note);
  }
  if (const std::string* text = document.resolve_entry(font, "BaseFont").name();
      text != nullptr && !text->empty()) {
    name_ = *text;
  }
  if (file_) {
    program_ = file_->kind();
  }
  const syntax::Object descendants = document.resolve_entry(font, "DescendantFonts");
  syntax::Object cid_font_object;
  if (const syntax::Object::Array* items = descendants.array();
      items != nullptr && !items->empty()) {
    cid_font_object = document.resolve(items->front());
  } else if (descendants.dictionary() != nullptr) {
    cid_font_object = descendants;
  }
  static const syntax::Dictionary empty;
  const syntax::Dictionary* cid_font = cid_font_object.dictionary();
  if (cid_font == nullptr) {
    cid_font = &empty;
    fault("the composite font has no descendant CIDFont");
  }
  true_type_glyphs_ = document.resolve_entry(*cid_font, "Subtype").is_name("CIDFontType2");
  const syntax::Object descriptor_object = document.resolve_entry(*cid_font, "FontDescriptor");
  const syntax::Dictionary* descriptor = descriptor_object.dictionary();
  if (descriptor == nullptr) {
    descriptor = &empty;
  }
  const std::uint32_t flags = static_cast<std::uint32_t>(
      document.resolve_entry(*descriptor, "Flags").integer().value_or(0));
  symbolic_ = (flags & flag_symbolic) != 0;
  const std::optional<double> weight = finite(document.resolve_entry(*descriptor, "FontWeight"));
  const std::string_view bare = name_;
  bold_ = (weight && *weight >= 600) || (flags & flag_force_bold) != 0 || contains(bare, "Bold") ||
          contains(bare, "bold") || contains(bare, "Black") || contains(bare, "Heavy");
  read_widths(document, *cid_font);
  widths_estimated_ = file_ == nullptr && !document.resolve_entry(*cid_font, "W").array();

  const std::optional<double> ascent = finite(document.resolve_entry(*descriptor, "Ascent"));
  const std::optional<double> descent = finite(document.resolve_entry(*descriptor, "Descent"));
  if (ascent && descent && *ascent != 0.0 && *ascent > *descent) {
    metrics_ = {*ascent, *descent, false};
  } else if (const std::optional<std::array<double, 2>> values =
                 file_ ? file_->ascent_descent() : std::nullopt) {
    metrics_ = {(*values)[0], (*values)[1], false};
  }
}

void Type0Font::read_widths(syntax::Document& document, const syntax::Dictionary& cid_font) {
  if (const std::optional<double> dw = finite(document.resolve_entry(cid_font, "DW"))) {
    default_width_ = *dw;
  }
  const syntax::Object w = document.resolve_entry(cid_font, "W");
  if (const syntax::Object::Array* items = w.array()) {
    std::size_t i = 0;
    while (i + 1 < items->size()) {
      const std::optional<std::uint32_t> first = cid_of(document.resolve((*items)[i]));
      const syntax::Object second = document.resolve((*items)[i + 1]);
      if (!first) {
        break;
      }
      if (const syntax::Object::Array* list = second.array()) {
        std::uint32_t cid = *first;
        for (const syntax::Object& item : *list) {
          if (const std::optional<double> width = finite(document.resolve(item))) {
            widths_[cid] = *width;
          }
          ++cid;
        }
        i += 2;
      } else if (const std::optional<std::uint32_t> last = cid_of(second);
                 last && i + 2 < items->size()) {
        if (const std::optional<double> width = finite(document.resolve((*items)[i + 2]))) {
          if (*last >= *first) {
            width_ranges_.push_back({*first, *last, *width});
          }
        }
        i += 3;
      } else {
        break;
      }
    }
  }
  const syntax::Object dw2 = document.resolve_entry(cid_font, "DW2");
  if (const syntax::Object::Array* items = dw2.array(); items != nullptr && items->size() == 2) {
    const std::optional<double> vy = finite(document.resolve((*items)[0]));
    const std::optional<double> w1 = finite(document.resolve((*items)[1]));
    if (vy && w1) {
      default_vertical_ = {*vy, *w1};
    }
  }
  const syntax::Object w2 = document.resolve_entry(cid_font, "W2");
  if (const syntax::Object::Array* items = w2.array()) {
    std::size_t i = 0;
    while (i + 1 < items->size()) {
      const std::optional<std::uint32_t> first = cid_of(document.resolve((*items)[i]));
      const syntax::Object second = document.resolve((*items)[i + 1]);
      if (!first) {
        break;
      }
      if (const syntax::Object::Array* list = second.array()) {
        std::uint32_t cid = *first;
        for (std::size_t k = 0; k + 2 < list->size(); k += 3) {
          const std::optional<double> a = finite(document.resolve((*list)[k]));
          const std::optional<double> b = finite(document.resolve((*list)[k + 1]));
          const std::optional<double> c = finite(document.resolve((*list)[k + 2]));
          if (a && b && c) {
            verticals_[cid] = {*a, *b, *c};
          }
          ++cid;
        }
        i += 2;
      } else if (const std::optional<std::uint32_t> last = cid_of(second);
                 last && i + 4 < items->size()) {
        const std::optional<double> a = finite(document.resolve((*items)[i + 2]));
        const std::optional<double> b = finite(document.resolve((*items)[i + 3]));
        const std::optional<double> c = finite(document.resolve((*items)[i + 4]));
        if (a && b && c && *last >= *first) {
          vertical_ranges_.push_back({*first, *last, {*a, *b, *c}});
        }
        i += 5;
      } else {
        break;
      }
    }
  }
}

std::pair<std::uint32_t, std::size_t> Type0Font::next_code(platform::ByteSpan bytes,
                                                           std::size_t at) const {
  const Cmap::Code code = encoding_->next(bytes, at);
  return {code.value, code.length};
}

PdfFont::Answer Type0Font::decode_code(std::uint32_t code) const {
  return DecodingLadder::composite(*this, code);
}

std::optional<std::uint32_t> Type0Font::glyph_for_cid(std::uint32_t cid) const {
  if (!file_) {
    return std::nullopt;
  }
  if (const CffFont* cff = file_->cff(); cff != nullptr && !true_type_glyphs_) {
    return cff->glyph_for_cid(cid);
  }
  if (cid_to_gid_) {
    if (cid < cid_to_gid_->size() && (*cid_to_gid_)[cid] != 0) {
      return (*cid_to_gid_)[cid];
    }
    return std::nullopt;
  }
  if (file_->cff() != nullptr) {
    return file_->cff()->glyph_for_cid(cid);
  }
  return cid == 0 ? std::nullopt : std::optional<std::uint32_t>(cid);
}

double Type0Font::cid_width(std::uint32_t cid) const {
  if (const auto found = widths_.find(cid); found != widths_.end()) {
    return found->second;
  }
  for (auto range = width_ranges_.rbegin(); range != width_ranges_.rend(); ++range) {
    if (cid >= range->first && cid <= range->last) {
      return range->width;
    }
  }
  return default_width_;
}

double Type0Font::code_width(std::uint32_t code) const {
  const std::optional<std::uint32_t> cid = encoding_->cid(code);
  return cid_width(cid.value_or(0));
}

PdfFont::VerticalAdvance Type0Font::vertical_advance(std::uint32_t code) const {
  const std::uint32_t cid = encoding_->cid(code).value_or(0);
  VerticalAdvance advance;
  advance.w1 = default_vertical_[1];
  advance.vx = cid_width(cid) / 2.0;
  advance.vy = default_vertical_[0];
  std::optional<std::array<double, 3>> values;
  if (const auto found = verticals_.find(cid); found != verticals_.end()) {
    values = found->second;
  } else {
    for (auto range = vertical_ranges_.rbegin(); range != vertical_ranges_.rend(); ++range) {
      if (cid >= range->first && cid <= range->last) {
        values = range->values;
        break;
      }
    }
  }
  if (values) {
    advance.w1 = (*values)[0];
    advance.vx = (*values)[1];
    advance.vy = (*values)[2];
  }
  return advance;
}

}  // namespace pdfkit::fonts
