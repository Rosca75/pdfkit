// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of fonts::Encodings: the codes ISO 32000-1 Annex D's footnotes call
// out (WinAnsi's Euro at 0x80, its non-breaking space and soft hyphen, its
// unassigned codes read as bullet; Standard's quotes; PDFDoc's own upper
// half), the encodings an /Encoding name selects, and the Standard, Symbol and
// ZapfDingbats tables checked code by code against the built-in encodings of
// the Core 14 AFM files, an independent source of the same facts.

#include "fonts/encodings.hpp"

#include "fonts/generated/core14_table.hpp"

#include <doctest.h>

#include <cstddef>
#include <string_view>

namespace {

using pdfkit::fonts::Encodings;
using Base = pdfkit::fonts::Encodings::Base;
using pdfkit::fonts::generated::Core14Table;

TEST_CASE("WinAnsi: the Euro, the spaces, the soft hyphen and the unassigned codes") {
  CHECK(Encodings::name(Base::win_ansi, 0x80) == "Euro");
  CHECK(Encodings::name(Base::win_ansi, 0xA0) == "space");
  CHECK(Encodings::name(Base::win_ansi, 0xAD) == "hyphen");
  CHECK(Encodings::name(Base::win_ansi, 0x27) == "quotesingle");
  CHECK(Encodings::name(Base::win_ansi, 0x60) == "grave");
  for (const int code : {0x7F, 0x81, 0x8D, 0x8F, 0x90, 0x9D}) {
    CHECK(Encodings::name(Base::win_ansi, static_cast<std::uint8_t>(code)) == "bullet");
  }
  CHECK(Encodings::name(Base::win_ansi, 0x0A).empty());
  CHECK(Encodings::name(Base::win_ansi, 0xE9) == "eacute");
}

TEST_CASE("Standard, MacRoman, MacExpert and PDFDoc spot checks") {
  CHECK(Encodings::name(Base::standard, 0x27) == "quoteright");
  CHECK(Encodings::name(Base::standard, 0x60) == "quoteleft");
  CHECK(Encodings::name(Base::standard, 0xA4) == "fraction");
  CHECK(Encodings::name(Base::standard, 0xAE) == "fi");
  CHECK(Encodings::name(Base::standard, 0xE9) == "Oslash");
  CHECK(Encodings::name(Base::standard, 0x80).empty());
  CHECK(Encodings::name(Base::mac_roman, 0x80) == "Adieresis");
  CHECK(Encodings::name(Base::mac_roman, 0xDE) == "fi");
  CHECK(Encodings::name(Base::mac_expert, 0x56) == "ff");
  CHECK(Encodings::name(Base::mac_expert, 0x57) == "fi");
  CHECK(Encodings::name(Base::pdf_doc, 0x18) == "breve");
  CHECK(Encodings::name(Base::pdf_doc, 0x80) == "bullet");
  CHECK(Encodings::name(Base::pdf_doc, 0x93) == "fi");
  CHECK(Encodings::name(Base::pdf_doc, 0xA0) == "Euro");
  CHECK(Encodings::name(Base::pdf_doc, 0xAD).empty());
  CHECK(Encodings::name(Base::pdf_doc, 0x41) == "A");
  CHECK(Encodings::name(Base::symbol, 0x61) == "alpha");
  CHECK(Encodings::name(Base::zapf_dingbats, 0x21) == "a1");
}

TEST_CASE("the encoding an /Encoding name selects") {
  CHECK(Encodings::by_name("WinAnsiEncoding") == Base::win_ansi);
  CHECK(Encodings::by_name("MacRomanEncoding") == Base::mac_roman);
  CHECK(Encodings::by_name("MacExpertEncoding") == Base::mac_expert);
  CHECK(Encodings::by_name("StandardEncoding") == Base::standard);
  CHECK_FALSE(Encodings::by_name("Identity-H").has_value());
  CHECK_FALSE(Encodings::by_name("SymbolEncoding").has_value());
}

// Every encoded glyph of an AFM file sits at its code in the table, and every
// name of the table at a code is the AFM glyph of that code.
void check_against_afm(std::size_t font, Base base) {
  const Core14Table::Font& entry = Core14Table::fonts()[font];
  CAPTURE(entry.name);
  std::size_t encoded = 0;
  for (std::size_t i = entry.first; i < std::size_t{entry.first} + entry.count; ++i) {
    const Core14Table::Glyph& glyph = Core14Table::glyphs()[i];
    if (glyph.code < 0) {
      continue;
    }
    ++encoded;
    CAPTURE(glyph.name);
    CHECK(Encodings::name(base, static_cast<std::uint8_t>(glyph.code)) == glyph.name);
  }
  std::size_t named = 0;
  for (std::size_t code = 0; code < 256; ++code) {
    if (!Encodings::name(base, static_cast<std::uint8_t>(code)).empty()) {
      ++named;
    }
  }
  CHECK(named == encoded);
}

TEST_CASE("Standard, Symbol and ZapfDingbats agree with the AFM files' codes") {
  check_against_afm(4, Base::standard);
  check_against_afm(0, Base::standard);
  check_against_afm(8, Base::standard);
  check_against_afm(12, Base::symbol);
  check_against_afm(13, Base::zapf_dingbats);
}

}  // namespace
