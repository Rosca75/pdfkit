// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the decoding ladder through FontLoader, one case per decoding
// path of docs/DESIGN.md section 5.4, each asserting the decoded text and the
// rung that answered: /ToUnicode correct, missing and wrong (where it wins
// over the glyph names, because the specification gives it precedence),
// /Differences with glyph-list names and with every naming convention, a
// symbolic TrueType font through its (3,0) cmap at 0xF000, a Type 1
// program's built-in encoding, a CFF charset, identity-encoded composite
// fonts, Type 3 fonts with and without /ToUnicode, a predefined CMap's codes
// read as unreadable and counted, a ligature decomposed, and an unreadable
// code that is never emitted as U+FFFD or a private-use character.

#include "fonts/decoding_ladder.hpp"

#include "fonts/decoding_rung.hpp"
#include "fonts/pdf_font.hpp"
#include "support/font_builder.hpp"
#include "support/font_document.hpp"

#include <doctest.h>

#include <string>
#include <vector>

namespace {

using pdfkit::fonts::DecodingRung;
using pdfkit::fonts::PdfFont;
using pdfkit::test::FontBuilder;
using pdfkit::test::FontDocument;
using Rungs = std::vector<DecodingRung>;

std::string cmap(const std::string& body) {
  return "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n" + body +
         "\nendcmap CMapName currentdict /CMap defineresource pop end end";
}

std::string to_unicode_one_byte(const std::string& entries) {
  return cmap("1 begincodespacerange <00> <FF> endcodespacerange\n" + entries);
}

// A TrueType program whose glyphs 1 to 3 are A, B and e-acute.
FontBuilder::TrueType latin_program() {
  FontBuilder::TrueType spec;
  spec.advances = {500, 600, 700, 800};
  spec.post_names = {".notdef", "A", "B", "eacute"};
  spec.cmaps.push_back({3, 1, 4, {{0x41, 1}, {0x42, 2}, {0xE9, 3}}});
  return spec;
}

TEST_CASE("a correct /ToUnicode answers first") {
  FontDocument doc;
  const auto map =
      doc.add_stream("", to_unicode_one_byte("2 beginbfchar <41> <0048> <42> <0069> endbfchar"));
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding "
      "/WinAnsiEncoding /ToUnicode " +
      std::to_string(map) + " 0 R >>");
  const auto decoded = FontDocument::decoded(*doc.load(font), "AB");
  CHECK(decoded.text == "Hi");
  CHECK(decoded.rungs == Rungs{DecodingRung::to_unicode, DecodingRung::to_unicode});
}

TEST_CASE("a missing /ToUnicode leaves the glyph names of the encoding to answer") {
  FontDocument doc;
  const auto font =
      doc.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
  const auto decoded = FontDocument::decoded(*doc.load(font), "Caf\xE9 \x80");
  CHECK(decoded.text == "Caf\xC3\xA9 \xE2\x82\xAC");
  CHECK(decoded.rungs == Rungs(6, DecodingRung::glyph_name));
}

TEST_CASE("a wrong /ToUnicode still wins over the glyph names") {
  // ISO 32000-1 section 9.10.2 puts /ToUnicode first; a map that contradicts
  // the glyphs drawn is the producer's statement of the text, and the
  // tounicode_wrong files are read as their map says, as PDFium reads them.
  FontDocument doc;
  const auto map = doc.add_stream("", to_unicode_one_byte("1 beginbfchar <41> <005A> endbfchar"));
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding "
      "/WinAnsiEncoding /ToUnicode " +
      std::to_string(map) + " 0 R >>");
  const auto decoded = FontDocument::decoded(*doc.load(font), "AB");
  CHECK(decoded.text == "ZB");
  CHECK(decoded.rungs == Rungs{DecodingRung::to_unicode, DecodingRung::glyph_name});
}

TEST_CASE("an empty, control or private-use /ToUnicode entry falls through") {
  FontDocument doc;
  const auto map = doc.add_stream(
      "", to_unicode_one_byte("3 beginbfchar <41> <> <42> <0001> <43> <E000> endbfchar"));
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding "
      "/WinAnsiEncoding /ToUnicode " +
      std::to_string(map) + " 0 R >>");
  const auto decoded = FontDocument::decoded(*doc.load(font), "ABC");
  CHECK(decoded.text == "ABC");
  CHECK(decoded.rungs == Rungs(3, DecodingRung::glyph_name));
}

TEST_CASE("/Differences with glyph-list names and every naming convention") {
  FontDocument doc;
  const auto encoding = doc.add(
      "<< /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [1 /Euro /uni0041 "
      "/uni00420043 /u1F600 /a.sc /T_h /f_f_i /nonsense /g7 /C65] >>");
  const auto font = doc.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding " +
                            std::to_string(encoding) + " 0 R >>");
  const auto loaded = doc.load(font);
  const auto decoded = FontDocument::decoded(*loaded, "\x01\x02\x03\x04\x05\x06\x07");
  CHECK(decoded.text ==
        "\xE2\x82\xAC"
        "ABC\xF0\x9F\x98\x80"
        "aThffi");
  CHECK(decoded.rungs == Rungs(7, DecodingRung::glyph_name));
  // A name nothing answers, and number names without a program, are
  // unreadable; the codes outside /Differences keep WinAnsi.
  const auto rest = FontDocument::decoded(*loaded,
                                          "\x08\x09\x0A"
                                          "A");
  CHECK(rest.text == "A");
  CHECK(rest.rungs == Rungs{DecodingRung::unreadable, DecodingRung::unreadable,
                            DecodingRung::unreadable, DecodingRung::glyph_name});
  CHECK(loaded->unreadable_count() == 3U);
}

TEST_CASE("gNN and Cdd names decode through the program's own map") {
  FontDocument doc;
  const auto program = doc.add_stream("", FontBuilder::true_type(latin_program()));
  const auto descriptor =
      doc.add("<< /Type /FontDescriptor /FontName /Latin /Flags 32 /FontFile2 " +
              std::to_string(program) + " 0 R >>");
  const auto encoding = doc.add("<< /Type /Encoding /Differences [1 /g2 /glyph3 /C65] >>");
  const auto font = doc.add("<< /Type /Font /Subtype /TrueType /BaseFont /Latin /FontDescriptor " +
                            std::to_string(descriptor) + " 0 R /Encoding " +
                            std::to_string(encoding) + " 0 R >>");
  const auto decoded = FontDocument::decoded(*doc.load(font), "\x01\x02\x03");
  CHECK(decoded.text ==
        "B\xC3\xA9"
        "A");
  CHECK(decoded.rungs == Rungs(3, DecodingRung::font_program));
}

TEST_CASE("a symbolic TrueType font finds its glyph through (3,0) at 0xF000") {
  FontDocument doc;
  FontBuilder::TrueType spec;
  spec.advances = {500, 600, 700};
  spec.post_names = {".notdef", "bullet", "Z"};
  spec.cmaps.push_back({3, 0, 4, {{0xF041, 1}, {0xF042, 2}}});
  const auto program = doc.add_stream("", FontBuilder::true_type(spec));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /Sym /Flags 4 /FontFile2 " +
                                  std::to_string(program) + " 0 R >>");
  const auto font = doc.add("<< /Type /Font /Subtype /TrueType /BaseFont /Sym /FontDescriptor " +
                            std::to_string(descriptor) + " 0 R >>");
  const auto loaded = doc.load(font);
  const auto decoded = FontDocument::decoded(*loaded, "ABC");
  CHECK(decoded.text == "\xE2\x80\xA2Z");
  CHECK(decoded.rungs ==
        Rungs{DecodingRung::font_program, DecodingRung::font_program, DecodingRung::unreadable});
  CHECK(loaded->is_symbolic());
}

TEST_CASE("a symbolic TrueType font with a Unicode cmap reads its glyphs' Unicode") {
  FontDocument doc;
  FontBuilder::TrueType spec = latin_program();
  spec.post_names.clear();
  const auto program = doc.add_stream("", FontBuilder::true_type(spec));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /Sym /Flags 4 /FontFile2 " +
                                  std::to_string(program) + " 0 R >>");
  const auto font = doc.add("<< /Type /Font /Subtype /TrueType /BaseFont /Sym /FontDescriptor " +
                            std::to_string(descriptor) + " 0 R >>");
  const auto decoded = FontDocument::decoded(*doc.load(font), "AB\xE9");
  CHECK(decoded.text == "AB\xC3\xA9");
  CHECK(decoded.rungs == Rungs(3, DecodingRung::font_program));
}

TEST_CASE("a Type 1 program's built-in encoding answers when /Encoding is absent") {
  FontDocument doc;
  FontBuilder::Type1 spec;
  spec.encoding = {{0x61, "Alpha"}, {0x62, "eacute"}, {0x63, "nothing_here"}};
  spec.glyphs = {{".notdef", 0}, {"Alpha", 700}, {"eacute", 500}, {"nothing_here", 300}};
  const auto program = doc.add_stream("/Length1 0 /Length2 0 /Length3 0", FontBuilder::type1(spec));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /Test /Flags 4 /FontFile " +
                                  std::to_string(program) + " 0 R >>");
  const auto font = doc.add("<< /Type /Font /Subtype /Type1 /BaseFont /Test /FontDescriptor " +
                            std::to_string(descriptor) + " 0 R >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->program() == PdfFont::Program::type1);
  const auto decoded = FontDocument::decoded(*loaded, "abc");
  CHECK(decoded.text == "\xCE\x91\xC3\xA9");
  CHECK(decoded.rungs ==
        Rungs{DecodingRung::font_program, DecodingRung::font_program, DecodingRung::unreadable});
}

TEST_CASE("a CFF program's encoding and charset answer for a Type1C font") {
  FontDocument doc;
  FontBuilder::Cff spec;
  spec.glyph_names = {".notdef", "A", "fi", "zeta.alt"};
  spec.widths = {std::nullopt, 0, 0, 0};
  spec.encoding = {{0x31, 1}, {0x32, 2}, {0x33, 3}};
  const auto program = doc.add_stream("/Subtype /Type1C", FontBuilder::cff(spec));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /C /Flags 4 /FontFile3 " +
                                  std::to_string(program) + " 0 R >>");
  const auto font = doc.add("<< /Type /Font /Subtype /Type1 /BaseFont /C /FontDescriptor " +
                            std::to_string(descriptor) + " 0 R >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->program() == PdfFont::Program::cff);
  const auto decoded = FontDocument::decoded(*loaded, "123");
  CHECK(decoded.text == "Afi\xCE\xB6");
  CHECK(decoded.rungs == Rungs(3, DecodingRung::font_program));
}

TEST_CASE("an identity-encoded composite font: /ToUnicode, then the program") {
  FontDocument doc;
  const auto program = doc.add_stream("", FontBuilder::true_type(latin_program()));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /L /Flags 32 /FontFile2 " +
                                  std::to_string(program) + " 0 R >>");
  const auto cid_font = doc.add(
      "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /L "
      "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) "
      "/Supplement 0 >> /FontDescriptor " +
      std::to_string(descriptor) + " 0 R /W [1 [500 600]] >>");
  const auto map = doc.add_stream("", cmap("1 begincodespacerange <0000> <FFFF> endcodespacerange "
                                           "1 beginbfchar <0003> <0045> endbfchar"));
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type0 /BaseFont /L /Encoding /Identity-H "
      "/DescendantFonts [" +
      std::to_string(cid_font) + " 0 R] /ToUnicode " + std::to_string(map) + " 0 R >>");
  const auto loaded = doc.load(font);
  CHECK(loaded->kind() == PdfFont::Kind::type0);
  const auto decoded =
      FontDocument::decoded(*loaded, std::string("\x00\x01\x00\x02\x00\x03\x00\x09", 8));
  CHECK(decoded.codes == std::vector<std::uint32_t>{1, 2, 3, 9});
  // Glyphs 1 and 2 by their post names, 3 by /ToUnicode, 9 by nothing.
  CHECK(decoded.text == "ABE");
  CHECK(decoded.rungs == Rungs{DecodingRung::glyph_name, DecodingRung::glyph_name,
                               DecodingRung::to_unicode, DecodingRung::unreadable});
}

TEST_CASE("a CIDFontType0 with a name-keyed CFF reads its charset's names") {
  FontDocument doc;
  FontBuilder::Cff spec;
  spec.glyph_names = {".notdef", "A", "B", "uni00E9"};
  spec.widths = {std::nullopt, 0, 0, 0};
  const auto program = doc.add_stream("/Subtype /CIDFontType0C", FontBuilder::cff(spec));
  const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /K /Flags 4 /FontFile3 " +
                                  std::to_string(program) + " 0 R >>");
  const auto cid_font =
      doc.add("<< /Type /Font /Subtype /CIDFontType0 /BaseFont /K /FontDescriptor " +
              std::to_string(descriptor) + " 0 R >>");
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type0 /BaseFont /K /Encoding /Identity-H "
      "/DescendantFonts [" +
      std::to_string(cid_font) + " 0 R] >>");
  const auto decoded =
      FontDocument::decoded(*doc.load(font), std::string("\x00\x01\x00\x03\x00\x04", 6));
  CHECK(decoded.text == "A\xC3\xA9");
  CHECK(decoded.rungs ==
        Rungs{DecodingRung::glyph_name, DecodingRung::glyph_name, DecodingRung::unreadable});
}

TEST_CASE("a Type 3 font reads /ToUnicode and its glyph names, and nothing more") {
  FontDocument doc;
  const auto map = doc.add_stream("", to_unicode_one_byte("1 beginbfchar <03> <0058> endbfchar"));
  const std::string common =
      "/Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] "
      "/CharProcs << >> /FirstChar 1 /LastChar 3 /Widths [500 500 500] /Encoding << /Type "
      "/Encoding /Differences [1 /a /g12 /C67] >>";
  const auto with = doc.add("<< " + common + " /ToUnicode " + std::to_string(map) + " 0 R >>");
  const auto without = doc.add("<< " + common + " >>");
  const auto read = FontDocument::decoded(*doc.load(with), "\x01\x02\x03");
  CHECK(read.text == "aX");
  CHECK(read.rungs ==
        Rungs{DecodingRung::glyph_name, DecodingRung::unreadable, DecodingRung::to_unicode});
  const auto bare = FontDocument::decoded(*doc.load(without), "\x01\x02\x03");
  CHECK(bare.text == "a");
  CHECK(bare.rungs ==
        Rungs{DecodingRung::glyph_name, DecodingRung::unreadable, DecodingRung::unreadable});
}

TEST_CASE("a predefined CMap without /ToUnicode reads every code as unreadable") {
  FontDocument doc;
  const auto cid_font = doc.add(
      "<< /Type /Font /Subtype /CIDFontType0 /BaseFont /Ryumin-Light "
      "/CIDSystemInfo << /Registry (Adobe) /Ordering (Japan1) "
      "/Supplement 2 >> >>");
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type0 /BaseFont /Ryumin-Light /Encoding "
      "/90ms-RKSJ-H /DescendantFonts [" +
      std::to_string(cid_font) + " 0 R] >>");
  const auto loaded = doc.load(font);
  // Shift-JIS: one byte for ASCII, two from a lead byte 0x81 to 0x9F.
  const auto decoded = FontDocument::decoded(*loaded,
                                             "A\x82\xA0"
                                             "B");
  CHECK(decoded.codes == std::vector<std::uint32_t>{0x41, 0x82A0, 0x42});
  CHECK(decoded.text.empty());
  CHECK(decoded.rungs == Rungs(3, DecodingRung::unreadable));
  CHECK(loaded->unreadable_count() == 3U);
}

TEST_CASE("a predefined Unicode CMap's codes are their characters") {
  FontDocument doc;
  const auto cid_font = doc.add("<< /Type /Font /Subtype /CIDFontType0 /BaseFont /KozMin >>");
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type0 /BaseFont /KozMin /Encoding "
      "/UniJIS-UTF16-H /DescendantFonts [" +
      std::to_string(cid_font) + " 0 R] >>");
  const auto decoded =
      FontDocument::decoded(*doc.load(font), std::string("\x65\xE5\xD8\x3D\xDE\x00", 6));
  CHECK(decoded.text == "\xE6\x97\xA5\xF0\x9F\x98\x80");
  CHECK(decoded.rungs == Rungs(2, DecodingRung::glyph_name));
}

TEST_CASE("a ligature glyph decomposes, one code standing for its letters") {
  FontDocument doc;
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding << "
      "/Differences [1 /fi /uniFB04] >> >>");
  const auto codes = doc.load(font)->decode(pdfkit::platform::as_bytes("\x01\x02"));
  REQUIRE(codes.size() == 2U);
  CHECK(codes[0].text == "fi");
  CHECK(codes[0].decomposed);
  CHECK(codes[1].text == "ffl");
  CHECK(codes[1].decomposed);
  CHECK(codes[1].rung == DecodingRung::glyph_name);
}

TEST_CASE("an unreadable code is never U+FFFD, a private-use character or (cid:N)") {
  FontDocument doc;
  const auto map =
      doc.add_stream("", to_unicode_one_byte("2 beginbfchar <01> <FFFD> <02> <F041> endbfchar"));
  const auto font = doc.add(
      "<< /Type /Font /Subtype /Type3 /FontMatrix [0.001 0 0 0.001 0 0] "
      "/Encoding << /Differences [1 /uniE001 /uniFFFD] >> /ToUnicode " +
      std::to_string(map) + " 0 R >>");
  const auto loaded = doc.load(font);
  for (const auto& code : loaded->decode(pdfkit::platform::as_bytes("\x01\x02\x03"))) {
    CHECK(code.rung == DecodingRung::unreadable);
    CHECK(code.text.empty());
  }
  CHECK(loaded->unreadable_count() == 3U);
}

TEST_CASE("a font resource naming nothing decodes nothing and never throws") {
  FontDocument doc;
  const auto broken = doc.add("[1 2 3]");
  const auto loaded = doc.load(broken);
  CHECK(FontDocument::decoded(*loaded, "AB").text.empty());
  CHECK_FALSE(loaded->faults().empty());
  const auto missing = doc.load(9999);
  CHECK(FontDocument::decoded(*missing, "A").rungs == Rungs{DecodingRung::unreadable});
}

TEST_CASE("a truncated or malformed program never throws out of the loader") {
  // Each program cut at several lengths, embedded under the entry that
  // names it: the font still loads and its codes decode through the
  // encoding's glyph names, the rung that remains.
  FontBuilder::Cff cff_spec;
  cff_spec.glyph_names = {".notdef", "A", "B"};
  cff_spec.widths = {std::nullopt, 10, 20};
  FontBuilder::Type1 type1_spec;
  type1_spec.glyphs = {{".notdef", 0}, {"A", 722}};
  const std::vector<std::pair<std::string, std::string>> programs = {
      {"FontFile2", FontBuilder::true_type(latin_program())},
      {"FontFile3", FontBuilder::cff(cff_spec)},
      {"FontFile", FontBuilder::type1(type1_spec)},
  };
  for (const auto& [entry, program] : programs) {
    for (const std::size_t cut : {std::size_t{0}, std::size_t{5}, std::size_t{13},
                                  program.size() / 3, program.size() / 2, program.size() - 1}) {
      CAPTURE(entry);
      CAPTURE(cut);
      FontDocument doc;
      const auto stream =
          doc.add_stream(entry == "FontFile3" ? "/Subtype /Type1C" : "", program.substr(0, cut));
      const auto descriptor = doc.add("<< /Type /FontDescriptor /FontName /T /Flags 32 /" + entry +
                                      " " + std::to_string(stream) + " 0 R >>");
      const auto font = doc.add("<< /Type /Font /Subtype /" +
                                std::string(entry == "FontFile2" ? "TrueType" : "Type1") +
                                " /BaseFont /T /Encoding /WinAnsiEncoding /FontDescriptor " +
                                std::to_string(descriptor) + " 0 R >>");
      std::shared_ptr<PdfFont> loaded;
      CHECK_NOTHROW(loaded = doc.load(font));
      REQUIRE(loaded != nullptr);
      const auto decoded = FontDocument::decoded(*loaded, "AB");
      CHECK(decoded.text == "AB");
      CHECK(decoded.rungs == Rungs(2, DecodingRung::glyph_name));
      CHECK_NOTHROW((void)loaded->width('A'));
    }
  }
}

}  // namespace
