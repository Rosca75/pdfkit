// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// SimpleFont is a Type 1, MMType1, TrueType or Type 3 font: one byte per
// code, docs/DESIGN.md sections 5.2 and 5.6. The encoding is built once: the
// base encoding is /Encoding as a name, or the dictionary's /BaseEncoding, or
// the embedded program's built-in encoding (a Type 1 or CFF program with its
// own encoding, when /Encoding is absent or the font is symbolic), or a
// non-embedded Core 14 face's built-in encoding, or Standard for a
// non-symbolic font (ISO 32000-1 section 9.6.6, read); /Differences replaces
// entries of it code by code. A symbolic TrueType font finds its glyph through
// the program's cmap in the order (3,0) with the code, (3,0) with the code
// plus 0xF000, (1,0) with the code, then (3,1) through the glyph name, which
// is pdf.js's and PDFBox's order (src/core/fonts.js, read; PDTrueTypeFont,
// recalled); (3,1) with the code itself closes the search, as pdf.js reads a
// symbolic font with a Unicode cmap only. Widths come from /Widths with
// /FirstChar (a code outside it takes /MissingWidth, default 0), else the
// program, else the Core 14 metrics; a Type 3 width is scaled through its
// /FontMatrix.

#ifndef PDFKIT_FONTS_SIMPLE_FONT_HPP
#define PDFKIT_FONTS_SIMPLE_FONT_HPP

#include "fonts/cmap.hpp"
#include "fonts/font_program.hpp"
#include "fonts/pdf_font.hpp"
#include "fonts/standard_fonts.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::fonts {

class SimpleFont : public PdfFont {
 public:
  // Where a code's glyph name came from: nowhere, the encoding the font
  // dictionary states (or Standard by default), or the program's own.
  enum class NameSource : std::uint8_t { none, encoding, program };

  // Builds the font from its dictionary; program and to_unicode are the
  // loader's (null when absent or unreadable), faults its notes so far.
  SimpleFont(syntax::Document& document, const syntax::Dictionary& font, Kind kind,
             std::uint32_t object_number, std::shared_ptr<const FontProgram> program,
             std::shared_ptr<const Cmap> to_unicode, const std::vector<std::string>& faults);

  // The font of a resource that names no font dictionary: no encoding, no
  // widths, every code unreadable.
  [[nodiscard]] static std::shared_ptr<SimpleFont> missing(syntax::Document& document,
                                                           std::uint32_t object_number,
                                                           const std::vector<std::string>& faults);

  [[nodiscard]] const Cmap* to_unicode() const noexcept { return to_unicode_.get(); }
  [[nodiscard]] const FontProgram* font_program() const noexcept { return file_.get(); }
  [[nodiscard]] std::optional<StandardFonts::Face> face() const noexcept { return face_; }
  // The glyph name of a code and where it came from.
  [[nodiscard]] std::string_view glyph_name(std::uint8_t code) const noexcept {
    return names_[code];
  }
  [[nodiscard]] NameSource name_source(std::uint8_t code) const noexcept { return sources_[code]; }
  // The glyph a TrueType or OpenType program draws for a code (the cmap
  // order above, then the post name); no value without one.
  [[nodiscard]] std::optional<std::uint32_t> true_type_glyph(std::uint32_t code) const;

 protected:
  [[nodiscard]] std::pair<std::uint32_t, std::size_t> next_code(platform::ByteSpan bytes,
                                                                std::size_t at) const override;
  [[nodiscard]] Answer decode_code(std::uint32_t code) const override;
  [[nodiscard]] double code_width(std::uint32_t code) const override;

 private:
  void read_encoding(syntax::Document& document, const syntax::Dictionary& font);
  void read_widths(syntax::Document& document, const syntax::Dictionary& font,
                   const syntax::Dictionary& descriptor);
  void read_metrics(syntax::Document& document, const syntax::Dictionary& font,
                    const syntax::Dictionary& descriptor);

  std::shared_ptr<const FontProgram> file_;
  std::shared_ptr<const Cmap> to_unicode_;
  std::optional<StandardFonts::Face> face_;
  std::array<std::string, 256> names_;
  std::array<NameSource, 256> sources_{};
  std::uint32_t flags_ = 0;
  std::int64_t first_char_ = 0;
  // /Widths in thousandths of text space; no value for an entry that is not
  // a number.
  std::vector<std::optional<double>> widths_;
  bool has_widths_ = false;
  double missing_width_ = 0.0;
  // A Type 3 font's /FontMatrix horizontal scale times 1000.
  double type3_scale_ = 1.0;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_SIMPLE_FONT_HPP
