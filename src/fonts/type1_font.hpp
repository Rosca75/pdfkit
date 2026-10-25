// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Type1Font reads what text extraction needs from a Type 1 program
// (/FontFile), docs/DESIGN.md section 5.7: PFB segment headers skipped, the
// clear-text /Encoding (StandardEncoding, or "dup <code> /<name> put"
// entries), /FontMatrix and /FontBBox, then the eexec section decrypted
// (r = 55665, four lead bytes, binary or hexadecimal), its /lenIV, and each
// CharStrings entry decrypted (r = 4330, lenIV lead bytes) and read as far as
// its hsbw or sbw command for the glyph's width. The format and the constants
// are those of Adobe's Type 1 Font Format (recalled; the unit tier encrypts a
// specimen with the same constants and reads it back). A program cut short
// answers what it read before the cut.

#ifndef PDFKIT_FONTS_TYPE1_FONT_HPP
#define PDFKIT_FONTS_TYPE1_FONT_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::fonts {

class Type1Font {
 public:
  // The program, or no value when the bytes hold no Type 1 font (no
  // "%!PS-AdobeFont", "%!FontType1" or PFB header and no /CharStrings).
  [[nodiscard]] static std::optional<Type1Font> parse(platform::ByteSpan data,
                                                      platform::Budget& budget);

  // Whether the clear text names StandardEncoding (or no encoding at all).
  [[nodiscard]] bool standard_encoding() const noexcept { return standard_encoding_; }
  // The glyph name the built-in encoding gives code; empty when none.
  [[nodiscard]] std::string_view encoding_name(std::uint8_t code) const noexcept;
  [[nodiscard]] bool has_glyph(std::string_view name) const;
  // A glyph's width in thousandths of text space (hsbw's or sbw's wx through
  // the font matrix), or no value for a glyph without one.
  [[nodiscard]] std::optional<double> width(std::string_view name) const;
  [[nodiscard]] std::size_t glyph_count() const noexcept { return widths_.size(); }
  // FontBBox in glyph space: llx, lly, urx, ury.
  [[nodiscard]] const std::array<double, 4>& bbox() const noexcept { return bbox_; }
  [[nodiscard]] double scale() const noexcept { return scale_; }

  // The eexec decryption of ISO's Type 1 format: r starts at key, c1 = 52845,
  // c2 = 22719. Exposed for the unit tier's specimen writer.
  [[nodiscard]] static std::vector<std::uint8_t> decrypt(const std::vector<std::uint8_t>& cipher,
                                                         std::uint16_t key);
  [[nodiscard]] static std::vector<std::uint8_t> encrypt(const std::vector<std::uint8_t>& plain,
                                                         std::uint16_t key);

 private:
  void read_clear_text(std::string_view text);
  void read_private(const std::vector<std::uint8_t>& plain, platform::Budget& budget);

  bool standard_encoding_ = true;
  std::array<std::string, 256> encoding_;
  // Each glyph's width in glyph space, or no value when its charstring gave
  // none.
  std::map<std::string, std::optional<double>, std::less<>> widths_;
  std::array<double, 4> bbox_{0.0, 0.0, 0.0, 0.0};
  double scale_ = 0.001;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_TYPE1_FONT_HPP
