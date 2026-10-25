// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PlaceholderFont is the library's own font for the placeholders a text
// redaction draws, docs/DESIGN.md section 13 and CLAUDE.md section 5 (a
// document's subset fonts routinely lack "[", "]", "_" and digits): a Type0
// font over a CIDFontType2 descendant with the Identity-H encoding and
// /CIDToGIDMap /Identity, so a code is a glyph id written in two bytes; /W
// from the subset's hmtx widths; a FontDescriptor holding the TrueType
// program as /FontFile2; and a /ToUnicode CMap mapping each code back to its
// character, so an extractor reads the placeholder (ISO 32000-1 sections
// 9.7, 9.8 and 9.10, read; PdfPig's CompositeTrueTypeWritingFont, Apache 2.0,
// read as reference only). The two faces, Regular and Bold, are subsets of
// Liberation Sans cut at build time by build/fonts/subset.py and compiled in
// as data with their hashes; each /BaseFont carries a subset tag derived
// from its program's hash, so it is stable. A face is embedded into an
// output once, on its first use, and only when a placeholder is drawn with
// it; the resource name it is drawn under is a function of its hash, so the
// same face is named alike in every dictionary it is added to.

#ifndef PDFKIT_WRITER_PLACEHOLDER_FONT_HPP
#define PDFKIT_WRITER_PLACEHOLDER_FONT_HPP

#include "syntax/object.hpp"
#include "writer/object_graph.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pdfkit::writer {

class PlaceholderFont {
 public:
  enum class Face : std::uint8_t { regular, bold };

  // The placeholder alphabet (CLAUDE.md section 4).
  static constexpr std::string_view alphabet = "[]_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  // What a face states about itself, in thousandths of an em.
  struct Metrics {
    std::string_view base_font;
    std::string_view sha256;
    int notdef_width = 0;
    int ascent = 0;
    int descent = 0;
    int cap_height = 0;
    std::array<int, 4> bbox{};
    int stem_v = 0;
    int weight = 0;
    std::span<const std::uint8_t> program;
  };

  // graph must outlive the font.
  explicit PlaceholderFont(ObjectGraph& graph) : graph_(&graph) {}

  // Whether every byte of text is a character of the alphabet.
  [[nodiscard]] static bool in_alphabet(std::string_view text) noexcept;
  // The first character of text outside the alphabet, if any.
  [[nodiscard]] static std::optional<char> outside(std::string_view text) noexcept;
  // The Identity-H codes of text, two bytes per character; text is in the
  // alphabet.
  [[nodiscard]] static std::string codes(std::string_view text, Face face);
  // The advance of text in thousandths of an em.
  [[nodiscard]] static double width(std::string_view text, Face face);
  [[nodiscard]] static const Metrics& metrics(Face face);
  // The resource name the face is drawn under.
  [[nodiscard]] static std::string resource_name(Face face);
  // The ToUnicode CMap of the face.
  [[nodiscard]] static std::string to_unicode(Face face);

  // The Type0 font dictionary of face in the graph, added with its
  // descendant, descriptor, program and CMap on the first call.
  [[nodiscard]] syntax::Object::Reference embed(Face face);
  // Whether face has been embedded.
  [[nodiscard]] bool embedded(Face face) const noexcept;

 private:
  ObjectGraph* graph_;
  std::array<std::optional<syntax::Object::Reference>, 2> embedded_;
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_PLACEHOLDER_FONT_HPP
