// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// CmapParser reads the CMap syntax of ISO 32000-1 sections 9.7.5 and 9.10.3
// (read) and Adobe Technical Note 5014 (recalled), docs/DESIGN.md section
// 5.3: begincodespacerange, begincidrange, begincidchar, beginnotdefrange,
// beginbfrange (a destination string incremented per code, or an array of
// strings), beginbfchar (a string, or a glyph name read through the glyph
// list), /WMode, and usecmap naming Identity-H or Identity-V, the only
// predefined CMaps whose data needs no table. A malformed entry is skipped and
// counted, because one bad line is not a reason to lose the map. The tokens
// are those of the PDF lexer, which CMap syntax shares.

#ifndef PDFKIT_FONTS_CMAP_PARSER_HPP
#define PDFKIT_FONTS_CMAP_PARSER_HPP

#include "fonts/cmap.hpp"
#include "platform/byte_span.hpp"

#include <string>

namespace pdfkit::fonts {

class CmapParser {
 public:
  // The CMap the bytes define. A usecmap naming a CMap other than the
  // identities is counted as malformed and ignored.
  [[nodiscard]] static Cmap parse(platform::ByteSpan bytes);

  // The UTF-16BE bytes of UTF-8 text, for a destination written as a name.
  [[nodiscard]] static std::string utf8_to_utf16be(const std::string& text);
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_CMAP_PARSER_HPP
