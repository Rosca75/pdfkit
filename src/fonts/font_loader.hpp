// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FontLoader turns a font dictionary into a PdfFont, docs/DESIGN.md section
// 5.1: it dispatches on /Subtype (Type1, MMType1, TrueType, Type3, Type0; a
// missing or unknown subtype is read as Type 1, as viewers read it, recalled),
// reads the embedded program, the /ToUnicode map and a composite font's CMap
// and /CIDToGIDMap, and caches each font per object number and each program
// and map per stream, because Document::decode charges the budget on every
// call and a font is asked for on every page that uses it. It never throws
// for a malformed font: a program, map or entry that does not read is
// dropped with a note on the font, and the font decodes through the rungs
// that remain. Only a refusal of the document itself (a decoding limit)
// passes through.

#ifndef PDFKIT_FONTS_FONT_LOADER_HPP
#define PDFKIT_FONTS_FONT_LOADER_HPP

#include "fonts/cmap.hpp"
#include "fonts/font_program.hpp"
#include "fonts/pdf_font.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pdfkit::fonts {

class FontLoader {
 public:
  // document must outlive the loader and every font it returns.
  explicit FontLoader(syntax::Document& document) : document_(&document) {}

  // The font a /Font resource entry names (a reference or a dictionary). A
  // value that is not a dictionary yields an empty font whose every code is
  // unreadable.
  [[nodiscard]] std::shared_ptr<PdfFont> load(const syntax::Object& font);

  // Fonts loaded so far, in the order first loaded.
  [[nodiscard]] const std::vector<std::shared_ptr<PdfFont>>& fonts() const noexcept {
    return order_;
  }

 private:
  [[nodiscard]] std::shared_ptr<PdfFont> build(const syntax::Dictionary& font,
                                               std::uint32_t object_number);
  [[nodiscard]] std::shared_ptr<const FontProgram> program(const syntax::Dictionary& descriptor,
                                                           std::vector<std::string>& faults);
  [[nodiscard]] std::shared_ptr<const Cmap> cmap_stream(const syntax::Object& value,
                                                        std::vector<std::string>& faults,
                                                        int depth = 0);
  [[nodiscard]] std::shared_ptr<const std::vector<std::uint16_t>> cid_to_gid(
      const syntax::Object& value, std::vector<std::string>& faults);

  syntax::Document* document_;
  std::map<std::uint32_t, std::shared_ptr<PdfFont>> by_object_;
  std::vector<std::shared_ptr<PdfFont>> order_;
  std::map<std::uint32_t, std::shared_ptr<const FontProgram>> programs_;
  std::map<std::uint32_t, std::string> program_faults_;
  std::map<std::uint32_t, std::shared_ptr<const Cmap>> cmaps_;
  std::map<std::uint32_t, std::shared_ptr<const std::vector<std::uint16_t>>> gid_maps_;
};

}  // namespace pdfkit::fonts

#endif  // PDFKIT_FONTS_FONT_LOADER_HPP
