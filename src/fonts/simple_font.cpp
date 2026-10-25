// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Everything is read once in the constructor; decoding and widths are then
// lookups. Values are resolved through the document and read leniently: an
// entry of the wrong type counts as absent, because one bad entry must not
// lose the font. The descriptor flags are those of ISO 32000-1 section
// 9.8.2, table 123 (read): bit 3 Symbolic (4), bit 6 Nonsymbolic (32), bit 19
// ForceBold (262144).

#include "fonts/simple_font.hpp"

#include "fonts/decoding_ladder.hpp"
#include "fonts/encodings.hpp"
#include "fonts/glyph_list.hpp"
#include "platform/utf8_sequence.hpp"

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

// The first code point of UTF-8 text the glyph list produced.
std::uint32_t first_code_point(const std::string& text) noexcept {
  const platform::Utf8Sequence sequence = platform::next_utf8_sequence(text, 0);
  return sequence.valid ? static_cast<std::uint32_t>(sequence.code_point) : 0;
}

bool name_says_bold(std::string_view name) noexcept {
  return name.find("Bold") != std::string_view::npos ||
         name.find("bold") != std::string_view::npos ||
         name.find("Black") != std::string_view::npos ||
         name.find("Heavy") != std::string_view::npos;
}

}  // namespace

SimpleFont::SimpleFont(syntax::Document& document, const syntax::Dictionary& font, Kind kind,
                       std::uint32_t object_number, std::shared_ptr<const FontProgram> program,
                       std::shared_ptr<const Cmap> to_unicode,
                       const std::vector<std::string>& faults)
    : file_(std::move(program)), to_unicode_(std::move(to_unicode)) {
  kind_ = kind;
  object_number_ = object_number;
  for (const std::string& note : faults) {
    fault(note);
  }
  const syntax::Object base_font =
      document.resolve_entry(font, kind == Kind::type3 ? "Name" : "BaseFont");
  if (const std::string* text = base_font.name(); text != nullptr && !text->empty()) {
    name_ = *text;
  } else if (kind == Kind::type3) {
    // A name points into its Object, so the Object is held while it is read.
    const syntax::Object fallback = document.resolve_entry(font, "BaseFont");
    if (const std::string* fallback_name = fallback.name()) {
      name_ = *fallback_name;
    }
  }
  if (file_) {
    program_ = file_->kind();
  }
  const syntax::Object descriptor_object = document.resolve_entry(font, "FontDescriptor");
  static const syntax::Dictionary empty;
  const syntax::Dictionary* descriptor = descriptor_object.dictionary();
  if (descriptor == nullptr) {
    descriptor = &empty;
  }
  if (const std::optional<std::int64_t> flags =
          document.resolve_entry(*descriptor, "Flags").integer()) {
    flags_ = static_cast<std::uint32_t>(*flags);
  }
  if (program_ == Program::none) {
    face_ = StandardFonts::find(name_);
  }
  symbolic_ = (flags_ & flag_symbolic) != 0 || (face_ && StandardFonts::is_symbolic(*face_));
  const std::optional<double> weight = finite(document.resolve_entry(*descriptor, "FontWeight"));
  bold_ = (weight && *weight >= 600) || (flags_ & flag_force_bold) != 0 ||
          name_says_bold(StandardFonts::strip_subset(name_)) ||
          (face_ && StandardFonts::is_bold(*face_));
  read_encoding(document, font);
  read_widths(document, font, *descriptor);
  read_metrics(document, font, *descriptor);
  widths_estimated_ = !has_widths_ && (program_ == Program::none) && !face_ && kind != Kind::type3;
}

std::shared_ptr<SimpleFont> SimpleFont::missing(syntax::Document& document,
                                                std::uint32_t object_number,
                                                const std::vector<std::string>& faults) {
  static const syntax::Dictionary empty;
  auto font = std::make_shared<SimpleFont>(document, empty, Kind::type1, object_number, nullptr,
                                           nullptr, faults);
  font->names_.fill(std::string());
  font->sources_.fill(NameSource::none);
  return font;
}

void SimpleFont::read_encoding(syntax::Document& document, const syntax::Dictionary& font) {
  const syntax::Object encoding = document.resolve_entry(font, "Encoding");
  std::optional<Encodings::Base> base;
  const syntax::Dictionary* dictionary = encoding.dictionary_or_stream_dictionary();
  if (const std::string* text = encoding.name()) {
    base = Encodings::by_name(*text);
  } else if (dictionary != nullptr) {
    const syntax::Object base_encoding = document.resolve_entry(*dictionary, "BaseEncoding");
    if (const std::string* base_name = base_encoding.name()) {
      base = Encodings::by_name(*base_name);
    }
  }
  if (base) {
    const Encodings::Table& table = Encodings::table(*base);
    for (std::size_t code = 0; code < 256; ++code) {
      names_[code] = std::string(table[code]);
      sources_[code] = names_[code].empty() ? NameSource::none : NameSource::encoding;
    }
  } else if (kind_ != Kind::type3) {
    const bool program_encoding =
        file_ && file_->has_custom_encoding() && (dictionary == nullptr || symbolic_);
    if (program_encoding) {
      for (std::size_t code = 0; code < 256; ++code) {
        names_[code] = std::string(file_->builtin_name(static_cast<std::uint8_t>(code)));
        sources_[code] = names_[code].empty() ? NameSource::none : NameSource::program;
      }
    } else if (face_) {
      for (std::size_t code = 0; code < 256; ++code) {
        names_[code] =
            std::string(StandardFonts::encoded_name(*face_, static_cast<std::uint8_t>(code)));
        sources_[code] = names_[code].empty() ? NameSource::none : NameSource::encoding;
      }
    } else if (!(symbolic_ && kind_ == Kind::true_type) &&
               !(symbolic_ && (program_ == Program::none))) {
      const Encodings::Table& table = Encodings::table(Encodings::Base::standard);
      for (std::size_t code = 0; code < 256; ++code) {
        names_[code] = std::string(table[code]);
        sources_[code] = names_[code].empty() ? NameSource::none : NameSource::encoding;
      }
    }
  }
  if (dictionary == nullptr) {
    return;
  }
  const syntax::Object differences = document.resolve_entry(*dictionary, "Differences");
  const syntax::Object::Array* items = differences.array();
  if (items == nullptr) {
    return;
  }
  std::int64_t code = -1;
  for (const syntax::Object& item : *items) {
    const syntax::Object value = document.resolve(item);
    if (const std::optional<std::int64_t> number = value.integer()) {
      code = *number;
    } else if (const std::optional<double> real = value.number()) {
      code = static_cast<std::int64_t>(*real);
    } else if (const std::string* glyph = value.name()) {
      if (code >= 0 && code < 256) {
        names_[static_cast<std::size_t>(code)] = *glyph;
        sources_[static_cast<std::size_t>(code)] =
            glyph->empty() || *glyph == ".notdef" ? NameSource::none : NameSource::encoding;
      }
      if (code >= 0) {
        ++code;
      }
    }
  }
}

void SimpleFont::read_widths(syntax::Document& document, const syntax::Dictionary& font,
                             const syntax::Dictionary& descriptor) {
  if (kind_ == Kind::type3) {
    const syntax::Object matrix = document.resolve_entry(font, "FontMatrix");
    if (const syntax::Object::Array* values = matrix.array();
        values != nullptr && !values->empty()) {
      if (const std::optional<double> a = finite(document.resolve((*values)[0]))) {
        type3_scale_ = *a * 1000.0;
      }
    } else {
      type3_scale_ = 1.0;
    }
  }
  if (const std::optional<double> missing =
          finite(document.resolve_entry(descriptor, "MissingWidth"))) {
    missing_width_ = *missing;
  }
  const syntax::Object widths = document.resolve_entry(font, "Widths");
  const syntax::Object::Array* items = widths.array();
  if (items == nullptr) {
    return;
  }
  has_widths_ = true;
  first_char_ = document.resolve_entry(font, "FirstChar").integer().value_or(0);
  widths_.reserve(std::min<std::size_t>(items->size(), 256));
  for (std::size_t i = 0; i < items->size() && i < 256; ++i) {
    widths_.push_back(finite(document.resolve((*items)[i])));
  }
}

void SimpleFont::read_metrics(syntax::Document& document, const syntax::Dictionary& font,
                              const syntax::Dictionary& descriptor) {
  if (kind_ == Kind::type3) {
    const syntax::Object bbox = document.resolve_entry(font, "FontBBox");
    const syntax::Object matrix = document.resolve_entry(font, "FontMatrix");
    const syntax::Object::Array* box = bbox.array();
    const syntax::Object::Array* m = matrix.array();
    if (box != nullptr && box->size() == 4 && m != nullptr && m->size() == 6) {
      const std::optional<double> lly = finite(document.resolve((*box)[1]));
      const std::optional<double> ury = finite(document.resolve((*box)[3]));
      const std::optional<double> d = finite(document.resolve((*m)[3]));
      if (lly && ury && d && *ury * *d > *lly * *d) {
        metrics_ = {*ury * *d * 1000.0, *lly * *d * 1000.0, false};
      }
    }
    return;
  }
  const std::optional<double> ascent = finite(document.resolve_entry(descriptor, "Ascent"));
  const std::optional<double> descent = finite(document.resolve_entry(descriptor, "Descent"));
  if (ascent && descent && *ascent != 0.0 && *ascent > *descent) {
    metrics_ = {*ascent, *descent, false};
    return;
  }
  if (file_) {
    if (const std::optional<std::array<double, 2>> values = file_->ascent_descent()) {
      metrics_ = {(*values)[0], (*values)[1], false};
      return;
    }
  }
  if (face_) {
    metrics_ = {static_cast<double>(StandardFonts::ascender(*face_)),
                static_cast<double>(StandardFonts::descender(*face_)), false};
    return;
  }
  const syntax::Object bbox = document.resolve_entry(descriptor, "FontBBox");
  if (const syntax::Object::Array* box = bbox.array(); box != nullptr && box->size() == 4) {
    const std::optional<double> lly = finite(document.resolve((*box)[1]));
    const std::optional<double> ury = finite(document.resolve((*box)[3]));
    if (lly && ury && *ury > *lly) {
      metrics_ = {*ury, *lly, false};
    }
  }
}

std::pair<std::uint32_t, std::size_t> SimpleFont::next_code(platform::ByteSpan bytes,
                                                            std::size_t at) const {
  return {std::to_integer<std::uint32_t>(bytes[at]), 1};
}

PdfFont::Answer SimpleFont::decode_code(std::uint32_t code) const {
  return DecodingLadder::simple(*this, code);
}

std::optional<std::uint32_t> SimpleFont::true_type_glyph(std::uint32_t code) const {
  const TrueTypeTables* tables = file_ ? file_->true_type() : nullptr;
  if (tables == nullptr || code > 0xFF) {
    return std::nullopt;
  }
  const std::string_view name = names_[code];
  const auto by_name = [&]() -> std::optional<std::uint32_t> {
    if (name.empty()) {
      return std::nullopt;
    }
    if (const std::optional<std::string> text = GlyphList::to_unicode(name)) {
      // The first code point of the name's text, looked up in (3,1).
      const std::uint32_t first = first_code_point(*text);
      if (const std::optional<std::uint32_t> glyph = tables->lookup(3, 1, first)) {
        return glyph;
      }
      if (const std::optional<std::uint32_t> glyph = tables->lookup(0, 3, first)) {
        return glyph;
      }
    }
    return tables->glyph_by_name(name);
  };
  if (symbolic_ || !tables->has_cmap(3, 1)) {
    for (const std::uint32_t candidate : {code, code + 0xF000U}) {
      if (const std::optional<std::uint32_t> glyph = tables->lookup(3, 0, candidate)) {
        return glyph;
      }
    }
    if (const std::optional<std::uint32_t> glyph = tables->lookup(1, 0, code)) {
      return glyph;
    }
    if (const std::optional<std::uint32_t> glyph = by_name()) {
      return glyph;
    }
    return tables->lookup(3, 1, code);
  }
  if (const std::optional<std::uint32_t> glyph = by_name()) {
    return glyph;
  }
  if (const std::optional<std::uint32_t> glyph = tables->lookup(1, 0, code)) {
    return glyph;
  }
  for (const std::uint32_t candidate : {code, code + 0xF000U}) {
    if (const std::optional<std::uint32_t> glyph = tables->lookup(3, 0, candidate)) {
      return glyph;
    }
  }
  return tables->lookup(3, 1, code);
}

double SimpleFont::code_width(std::uint32_t code) const {
  if (has_widths_) {
    const std::int64_t index = static_cast<std::int64_t>(code) - first_char_;
    const std::optional<double> stated =
        index >= 0 && static_cast<std::size_t>(index) < widths_.size()
            ? widths_[static_cast<std::size_t>(index)]
            : std::nullopt;
    if (stated) {
      return kind_ == Kind::type3 ? *stated * type3_scale_ : *stated;
    }
    return kind_ == Kind::type3 ? missing_width_ * type3_scale_ : missing_width_;
  }
  if (code > 0xFF) {
    return missing_width_;
  }
  const std::string_view name = names_[code];
  if (file_) {
    if (file_->true_type() != nullptr) {
      if (const std::optional<std::uint32_t> glyph = true_type_glyph(code)) {
        if (const std::optional<double> value = file_->width_of_glyph(*glyph)) {
          return *value;
        }
      }
    } else if (!name.empty()) {
      if (const std::optional<double> value = file_->width_of_name(name)) {
        return *value;
      }
    } else if (const CffFont* cff = file_->cff(); cff != nullptr && !cff->is_cid()) {
      if (const std::optional<std::uint32_t> glyph =
              cff->glyph_for_code(static_cast<std::uint8_t>(code))) {
        if (const std::optional<double> value = cff->width(*glyph)) {
          return *value;
        }
      }
    }
  }
  if (face_ && !name.empty()) {
    if (const std::optional<int> value = StandardFonts::width(*face_, name)) {
      return *value;
    }
  }
  return missing_width_;
}

}  // namespace pdfkit::fonts
