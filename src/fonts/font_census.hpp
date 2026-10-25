// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FontCensus measures the fonts layer over one document before the content
// interpreter exists: every font reachable from every page's resources and
// its forms' resources is loaded, and every string operand of Tj, TJ, ' and "
// is decoded in the font the preceding Tf named, in the lexical order of the
// content lexer. That order is an approximation of the interpreter, not the
// interpreter: a form drawn twice is walked once, a font set inside a form is
// carried out of it only as q and Q allow, and nothing is positioned. The
// content interpreter replaces this census as the measure of the fonts layer.

#ifndef PDFKIT_FONTS_FONT_CENSUS_HPP
#define PDFKIT_FONTS_FONT_CENSUS_HPP

#include "fonts/decoding_rung.hpp"
#include "syntax/document.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pdfkit::fonts {

class FontCensus {
 public:
  // One font dictionary the walk loaded.
  struct Font {
    std::uint32_t object_number = 0;
    std::string name;
    std::string kind;
    std::string program;
    std::string encoding;
    bool to_unicode = false;
    std::uint64_t codes = 0;
    std::uint64_t unreadable = 0;
    // Codes per rung, in the order of DecodingRung.
    std::array<std::uint64_t, 5> rungs{};
    std::vector<std::string> faults;
  };

  struct Report {
    std::vector<Font> fonts;
    // The decoded text of each page, in lexical order.
    std::vector<std::string> pages;
    std::uint64_t codes = 0;
    std::uint64_t unreadable = 0;
    std::array<std::uint64_t, 5> rungs{};
    // Font resources that resolved to nothing a font could be built from.
    std::uint64_t missing_fonts = 0;
  };

  // Walks every page of document. Throws only what the document itself
  // refuses (a decoding limit).
  [[nodiscard]] static Report run(syntax::Document& document);
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_FONT_CENSUS_HPP
