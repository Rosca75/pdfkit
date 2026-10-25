// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// CffFont reads what text extraction needs from a Compact Font Format
// program, bare (/FontFile3 /Type1C or /CIDFontType0C) or the CFF table of an
// OpenType program, docs/DESIGN.md section 5.7: the header, the INDEX
// structures, the top DICT, the string INDEX, the charsets (formats 0, 1 and 2
// and the predefined ISOAdobe, Expert and ExpertSubset), the encodings
// (standard, expert, or custom with supplements), each private DICT's
// defaultWidthX and nominalWidthX, and the CID-keyed FDArray and FDSelect. A
// Type 2 charstring is read only as far as its first stack-clearing operator,
// which carries the glyph's width when its operand count says so. The layout
// is that of Adobe Technical Notes 5176 (CFF) and 5177 (Type 2 charstrings),
// recalled, with pdf.js src/core/cff_parser.js (Apache-2.0, read) beside it;
// the standard strings and the predefined charsets are ported from pdf.js.
// A malformed structure ends the reading of that structure, and what was read
// before the fault still answers.

#ifndef PDFKIT_FONTS_CFF_FONT_HPP
#define PDFKIT_FONTS_CFF_FONT_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::fonts {

class CffFont {
 public:
  // The program, or no value when the bytes hold no CFF header or no
  // CharStrings INDEX. Each count is checked against budget first.
  [[nodiscard]] static std::optional<CffFont> parse(platform::ByteSpan data,
                                                    platform::Budget& budget);

  [[nodiscard]] bool is_cid() const noexcept { return cid_keyed_; }
  [[nodiscard]] std::uint32_t glyph_count() const noexcept { return glyph_count_; }

  // The charset name of a glyph of a name-keyed font; empty for a CID-keyed
  // font or a glyph past the charset.
  [[nodiscard]] std::string_view glyph_name(std::uint32_t glyph) const noexcept;
  [[nodiscard]] std::optional<std::uint32_t> glyph_by_name(std::string_view name) const;
  // The glyph of a CID: through the charset of a CID-keyed font, the glyph
  // index itself in a name-keyed font.
  [[nodiscard]] std::optional<std::uint32_t> glyph_for_cid(std::uint32_t cid) const;

  // Whether the top DICT names the standard encoding (or none, which means
  // the standard encoding).
  [[nodiscard]] bool standard_encoding() const noexcept { return standard_encoding_; }
  // The glyph the program's own encoding gives code, or no value.
  [[nodiscard]] std::optional<std::uint32_t> glyph_for_code(std::uint8_t code) const;

  // The width of a glyph in thousandths of text space (the charstring's
  // width or the private DICT's default, through the font matrix); no value
  // past the CharStrings INDEX.
  [[nodiscard]] std::optional<double> width(std::uint32_t glyph) const;

  // The font matrix's horizontal scale (0.001 by default).
  [[nodiscard]] double scale() const noexcept { return scale_; }
  // FontBBox in glyph space: llx, lly, urx, ury.
  [[nodiscard]] const std::array<double, 4>& bbox() const noexcept { return bbox_; }

 private:
  struct Index {
    std::size_t data_start = 0;
    // count + 1 offsets, relative to data_start.
    std::vector<std::uint32_t> offsets;
    [[nodiscard]] std::size_t count() const noexcept {
      return offsets.empty() ? 0 : offsets.size() - 1;
    }
  };
  struct Private {
    double default_width = 0.0;
    double nominal_width = 0.0;
    double scale = 0.001;
    Index subrs;
  };

  [[nodiscard]] platform::ByteSpan bytes() const noexcept { return *data_; }
  [[nodiscard]] static std::optional<Index> read_index(platform::ByteSpan data, std::size_t& at,
                                                       platform::Budget& budget);
  [[nodiscard]] platform::ByteSpan object(const Index& index, std::size_t i) const noexcept;
  [[nodiscard]] std::string string_of(std::uint32_t sid) const;
  [[nodiscard]] Private read_private(platform::ByteSpan dict, double scale,
                                     platform::Budget& budget) const;
  void read_charset(std::size_t offset, platform::Budget& budget);
  void read_encoding(std::size_t offset);
  void read_fd_select(std::size_t offset, platform::Budget& budget);
  [[nodiscard]] std::optional<double> charstring_width(platform::ByteSpan charstring,
                                                       const Private& priv) const;

  std::shared_ptr<const std::vector<std::byte>> data_;
  bool cid_keyed_ = false;
  std::uint32_t glyph_count_ = 0;
  Index strings_;
  Index global_subrs_;
  Index charstrings_;
  std::vector<Private> privates_;
  std::vector<std::uint8_t> fd_select_;
  std::vector<std::string> names_;
  std::vector<std::uint32_t> cids_;
  std::map<std::string, std::uint32_t, std::less<>> by_name_;
  std::map<std::uint32_t, std::uint32_t> by_cid_;
  bool standard_encoding_ = true;
  bool expert_encoding_ = false;
  std::map<std::uint8_t, std::uint32_t> encoding_;
  double scale_ = 0.001;
  std::array<double, 4> bbox_{0.0, 0.0, 0.0, 0.0};
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_CFF_FONT_HPP
