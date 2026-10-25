// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The bytes decide the reader: an sfnt header is TrueType or OpenType
// whatever the entry says, a CFF header (major version 1) is CFF, and
// anything else under /FontFile is tried as Type 1. A reader's fault is
// caught here, so a malformed program costs only itself.

#include "fonts/font_program.hpp"

#include "platform/error.hpp"

#include <exception>

namespace pdfkit::fonts {

FontProgram FontProgram::read(platform::ByteSpan bytes, Entry entry, std::string_view subtype,
                              platform::Budget& budget, std::string& fault) {
  FontProgram program;
  if (bytes.empty()) {
    fault = "the font program stream is empty";
    return program;
  }
  try {
    if (TrueTypeTables::looks_like(bytes)) {
      program.true_type_ = TrueTypeTables::parse(bytes, budget);
      if (program.true_type_ && !program.true_type_->cff().empty()) {
        program.cff_ = CffFont::parse(program.true_type_->cff(), budget);
        program.kind_ = PdfFont::Program::open_type;
      } else if (program.true_type_) {
        program.kind_ = PdfFont::Program::true_type;
      }
    } else if (std::to_integer<int>(bytes[0]) == 1 && bytes.size() > 4) {
      program.cff_ = CffFont::parse(bytes, budget);
      if (program.cff_) {
        program.kind_ = PdfFont::Program::cff;
      }
    } else if (entry == Entry::font_file || subtype.empty() || subtype == "Type1") {
      program.type1_ = Type1Font::parse(bytes, budget);
      if (program.type1_) {
        program.kind_ = PdfFont::Program::type1;
      }
    }
  } catch (const platform::Error& error) {
    program = FontProgram();
    fault = "the font program could not be read: " + error.failed();
    return program;
  } catch (const std::exception& error) {
    program = FontProgram();
    fault = std::string("the font program could not be read: ") + error.what();
    return program;
  }
  if (program.kind_ == PdfFont::Program::none) {
    fault =
        "the font program is not a TrueType, OpenType, CFF or Type 1 program a reader "
        "recognises";
  }
  return program;
}

std::string_view FontProgram::builtin_name(std::uint8_t code) const {
  if (type1_) {
    return type1_->encoding_name(code);
  }
  if (cff_ && !cff_->is_cid()) {
    if (const std::optional<std::uint32_t> glyph = cff_->glyph_for_code(code)) {
      return cff_->glyph_name(*glyph);
    }
  }
  return {};
}

bool FontProgram::has_custom_encoding() const noexcept {
  if (type1_) {
    return !type1_->standard_encoding();
  }
  if (cff_ && !cff_->is_cid() && kind_ == PdfFont::Program::cff) {
    return !cff_->standard_encoding();
  }
  return false;
}

std::optional<double> FontProgram::width_of_name(std::string_view name) const {
  if (type1_) {
    return type1_->width(name);
  }
  if (cff_ && !cff_->is_cid()) {
    if (const std::optional<std::uint32_t> glyph = cff_->glyph_by_name(name)) {
      return cff_->width(*glyph);
    }
    return std::nullopt;
  }
  if (true_type_) {
    if (const std::optional<std::uint32_t> glyph = true_type_->glyph_by_name(name)) {
      return width_of_glyph(*glyph);
    }
  }
  return std::nullopt;
}

std::optional<double> FontProgram::width_of_glyph(std::uint32_t glyph) const {
  if (true_type_) {
    if (const std::optional<std::uint16_t> advance = true_type_->advance(glyph)) {
      return *advance * 1000.0 / true_type_->units_per_em();
    }
  }
  if (cff_) {
    return cff_->width(glyph);
  }
  return std::nullopt;
}

std::optional<std::array<double, 2>> FontProgram::ascent_descent() const {
  if (true_type_ && (true_type_->ascender() != 0 || true_type_->descender() != 0)) {
    const double scale = 1000.0 / true_type_->units_per_em();
    return std::array<double, 2>{true_type_->ascender() * scale, true_type_->descender() * scale};
  }
  const std::array<double, 4>* bbox = nullptr;
  double scale = 1.0;
  if (cff_) {
    bbox = &cff_->bbox();
    scale = cff_->scale() * 1000.0;
  } else if (type1_) {
    bbox = &type1_->bbox();
    scale = type1_->scale() * 1000.0;
  }
  if (bbox == nullptr || (*bbox)[3] <= (*bbox)[1]) {
    return std::nullopt;
  }
  return std::array<double, 2>{(*bbox)[3] * scale, (*bbox)[1] * scale};
}

}  // namespace pdfkit::fonts
