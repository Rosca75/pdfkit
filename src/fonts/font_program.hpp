// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FontProgram is the embedded program of one font descriptor, read by the
// reader its stream names, docs/DESIGN.md section 5.7: /FontFile (Type 1),
// /FontFile2 (TrueType) and /FontFile3 (/Type1C and /CIDFontType0C as bare
// CFF, /OpenType as an OpenType program whose CFF table is read too). The
// subtype is a claim, so the bytes are sniffed and the reader that
// recognises them is used. A program that no reader accepts, or whose reader
// faults, is dropped with a note, and the font decodes through the rungs that
// remain. One program shared by several fonts is read once per document.

#ifndef PDFKIT_FONTS_FONT_PROGRAM_HPP
#define PDFKIT_FONTS_FONT_PROGRAM_HPP

#include "fonts/cff_font.hpp"
#include "fonts/pdf_font.hpp"
#include "fonts/true_type_tables.hpp"
#include "fonts/type1_font.hpp"
#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pdfkit::fonts {

class FontProgram {
 public:
  // Which descriptor entry held the program, and its /Subtype for FontFile3.
  enum class Entry : std::uint8_t { font_file, font_file2, font_file3 };

  // Reads decoded program bytes. fault receives why nothing was read; the
  // result's kind is then PdfFont::Program::none.
  [[nodiscard]] static FontProgram read(platform::ByteSpan bytes, Entry entry,
                                        std::string_view subtype, platform::Budget& budget,
                                        std::string& fault);

  [[nodiscard]] PdfFont::Program kind() const noexcept { return kind_; }
  [[nodiscard]] const TrueTypeTables* true_type() const noexcept {
    return true_type_ ? &*true_type_ : nullptr;
  }
  [[nodiscard]] const CffFont* cff() const noexcept { return cff_ ? &*cff_ : nullptr; }
  [[nodiscard]] const Type1Font* type1() const noexcept { return type1_ ? &*type1_ : nullptr; }

  // The glyph name the program's own encoding gives code (a Type 1 or CFF
  // built-in encoding); empty when there is none.
  [[nodiscard]] std::string_view builtin_name(std::uint8_t code) const;
  // Whether the program carries a built-in encoding other than
  // StandardEncoding.
  [[nodiscard]] bool has_custom_encoding() const noexcept;
  // A glyph's width in thousandths of text space, by name (Type 1, CFF,
  // TrueType post) or by glyph index (CFF, TrueType).
  [[nodiscard]] std::optional<double> width_of_name(std::string_view name) const;
  [[nodiscard]] std::optional<double> width_of_glyph(std::uint32_t glyph) const;
  // Ascent and descent in thousandths of text space (hhea, or the bounding
  // box of a CFF or Type 1 program); no value when the program states none.
  [[nodiscard]] std::optional<std::array<double, 2>> ascent_descent() const;

 private:
  PdfFont::Program kind_ = PdfFont::Program::none;
  std::optional<TrueTypeTables> true_type_;
  std::optional<CffFont> cff_;
  std::optional<Type1Font> type1_;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_FONT_PROGRAM_HPP
