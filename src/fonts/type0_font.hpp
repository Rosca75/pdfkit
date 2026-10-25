// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Type0Font is a composite font, docs/DESIGN.md section 5.3 and ISO 32000-1
// section 9.7 (read): /Encoding is Identity-H or Identity-V (two-byte codes,
// CID equal to the code), an embedded CMap stream (with its /UseCMap), or a
// predefined CMap name. A predefined CMap other than the identities ships no
// data, so its codes are split by the codespace its name implies (the byte
// structure of Shift-JIS, EUC, Big5 and the Unicode CMaps, recalled from
// Adobe's CMap resources) and, without /ToUnicode, read as unreadable; the
// Unicode CMaps (Uni...-UCS2, -UTF16, -UTF8, -UTF32) are the exception,
// because their codes are the characters themselves. The descendant is a
// CIDFontType0 (CFF glyphs, CID to glyph through the CFF charset) or a
// CIDFontType2 (TrueType glyphs, CID to glyph through /CIDToGIDMap, Identity
// or a stream of two-byte glyph ids). Widths come from /W in both array forms
// and /DW (default 1000); /W2 and /DW2 (default [880 -1000]) are kept for
// vertical writing.

#ifndef PDFKIT_FONTS_TYPE0_FONT_HPP
#define PDFKIT_FONTS_TYPE0_FONT_HPP

#include "fonts/cmap.hpp"
#include "fonts/font_program.hpp"
#include "fonts/pdf_font.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::fonts {

class Type0Font : public PdfFont {
 public:
  // How the codes of a predefined Unicode CMap carry their characters.
  enum class UnicodeCodes : std::uint8_t { none, ucs2, utf16, utf8, utf32 };

  // Builds the font from its dictionary: encoding is the code-to-CID map
  // (never null), predefined the name of a predefined non-identity CMap
  // (empty otherwise); program, cid_to_gid and to_unicode are the loader's.
  Type0Font(syntax::Document& document, const syntax::Dictionary& font, std::uint32_t object_number,
            std::shared_ptr<const Cmap> encoding, std::string predefined,
            std::shared_ptr<const FontProgram> program,
            std::shared_ptr<const std::vector<std::uint16_t>> cid_to_gid,
            std::shared_ptr<const Cmap> to_unicode, const std::vector<std::string>& faults);

  // The codespace a predefined CMap's name implies, and whether its codes
  // are Unicode.
  [[nodiscard]] static Cmap predefined_codespace(std::string_view name);
  [[nodiscard]] static UnicodeCodes unicode_codes(std::string_view name) noexcept;

  [[nodiscard]] const Cmap& encoding() const noexcept { return *encoding_; }
  [[nodiscard]] const Cmap* to_unicode() const noexcept { return to_unicode_.get(); }
  [[nodiscard]] const FontProgram* font_program() const noexcept { return file_.get(); }
  // The predefined non-identity CMap's name; empty otherwise.
  [[nodiscard]] const std::string& predefined() const noexcept { return predefined_; }
  [[nodiscard]] UnicodeCodes codes_are_unicode() const noexcept { return unicode_codes_; }
  // Whether the descendant is a CIDFontType2.
  [[nodiscard]] bool true_type_glyphs() const noexcept { return true_type_glyphs_; }
  // The glyph of a CID in the program; no value without a program or a
  // glyph.
  [[nodiscard]] std::optional<std::uint32_t> glyph_for_cid(std::uint32_t cid) const;

  [[nodiscard]] VerticalAdvance vertical_advance(std::uint32_t code) const override;

 protected:
  [[nodiscard]] std::pair<std::uint32_t, std::size_t> next_code(platform::ByteSpan bytes,
                                                                std::size_t at) const override;
  [[nodiscard]] Answer decode_code(std::uint32_t code) const override;
  [[nodiscard]] double code_width(std::uint32_t code) const override;

 private:
  struct WidthRange {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    double width = 0.0;
  };
  struct VerticalRange {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    std::array<double, 3> values{0.0, 0.0, 0.0};
  };

  void read_widths(syntax::Document& document, const syntax::Dictionary& cid_font);
  [[nodiscard]] double cid_width(std::uint32_t cid) const;

  std::shared_ptr<const Cmap> encoding_;
  std::string predefined_;
  UnicodeCodes unicode_codes_ = UnicodeCodes::none;
  std::shared_ptr<const FontProgram> file_;
  std::shared_ptr<const std::vector<std::uint16_t>> cid_to_gid_;
  std::shared_ptr<const Cmap> to_unicode_;
  bool true_type_glyphs_ = false;
  double default_width_ = 1000.0;
  std::map<std::uint32_t, double> widths_;
  std::vector<WidthRange> width_ranges_;
  std::array<double, 2> default_vertical_{880.0, -1000.0};
  std::map<std::uint32_t, std::array<double, 3>> verticals_;
  std::vector<VerticalRange> vertical_ranges_;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_TYPE0_FONT_HPP
