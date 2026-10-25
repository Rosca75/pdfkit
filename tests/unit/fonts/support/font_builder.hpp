// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FontBuilder writes the font programs the fonts tests read, so no font file
// is committed: a minimal TrueType program (cmap subtables of formats 0, 4, 6
// and 12, head, hhea, hmtx, maxp, post format 2, and loca and glyf when asked),
// a minimal CFF program (name-keyed with a charset, an encoding and private
// widths, or CID-keyed with an FDArray and FDSelect), and a minimal Type 1
// program (clear text, then the private part eexec-encrypted, binary or
// hexadecimal, optionally in PFB segments). Each writer follows the layout its
// reader reads, so a test of a reader is a test against the format as written
// here from the specification, and a truncated program is a substring.

#ifndef PDFKIT_TESTS_UNIT_FONTS_SUPPORT_FONT_BUILDER_HPP
#define PDFKIT_TESTS_UNIT_FONTS_SUPPORT_FONT_BUILDER_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::test {

class FontBuilder {
 public:
  struct CmapSubtable {
    std::uint16_t platform = 3;
    std::uint16_t encoding = 1;
    std::uint16_t format = 4;
    std::map<std::uint32_t, std::uint16_t> map;
  };

  struct TrueType {
    std::uint16_t units_per_em = 1000;
    // hmtx's advances; their count is hhea's metric count.
    std::vector<std::uint16_t> advances;
    // maxp's glyph count; the advance count when zero.
    std::uint16_t glyph_count = 0;
    // post format 2 names by glyph; no post table when empty.
    std::vector<std::string> post_names;
    std::vector<CmapSubtable> cmaps;
    std::int16_t ascender = 800;
    std::int16_t descender = -200;
    // Whether each glyph is empty; no loca and glyf when empty.
    std::vector<bool> empty_glyphs;
    // A CFF table, making the program OpenType.
    std::string cff;
  };

  struct Cff {
    // Name-keyed: the charset names by glyph (glyph 0 is .notdef).
    std::vector<std::string> glyph_names;
    // CID-keyed when not empty: the CID of each glyph (glyph 0 is CID 0).
    std::vector<std::uint16_t> cids;
    // The charstring width operand per glyph (the width minus
    // nominalWidthX); no value writes no width, so the default applies.
    std::vector<std::optional<int>> widths;
    int default_width = 0;
    int nominal_width = 0;
    // A custom encoding, code to glyph; the standard encoding when empty.
    std::map<std::uint8_t, std::uint16_t> encoding;
    // 0, 1 or 2.
    int charset_format = 0;
    // The first glyph's width is written in a local subroutine the
    // charstring calls.
    bool width_in_subroutine = false;
  };

  struct Type1 {
    std::string font_name = "Test";
    // code to glyph name; StandardEncoding when empty.
    std::map<std::uint8_t, std::string> encoding;
    // name and hsbw width, in order.
    std::vector<std::pair<std::string, int>> glyphs;
    bool hex_eexec = false;
    bool pfb = false;
    // The eexec section is cut to this many bytes when set.
    std::optional<std::size_t> truncate_eexec;
    // The width command: hsbw, or sbw when true.
    bool sbw = false;
  };

  [[nodiscard]] static std::string true_type(const TrueType& spec);
  [[nodiscard]] static std::string cff(const Cff& spec);
  [[nodiscard]] static std::string type1(const Type1& spec);
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_UNIT_FONTS_SUPPORT_FONT_BUILDER_HPP
