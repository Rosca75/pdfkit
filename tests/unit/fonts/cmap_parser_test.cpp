// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of fonts::CmapParser and fonts::Cmap: every operator, codespaces of
// one to four bytes mixed in one map, the code length decided shortest first,
// a byte sequence outside every range, surrogate pairs kept whole, the bfrange
// array form, a destination incremented past its last byte, a range that
// crosses a byte boundary, usecmap of an identity, a name destination,
// /WMode, and malformed entries skipped and counted.

#include "fonts/cmap_parser.hpp"

#include "fonts/cmap.hpp"
#include "platform/byte_span.hpp"
#include "support/require_value.hpp"

#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

using pdfkit::fonts::Cmap;
using pdfkit::fonts::CmapParser;
using pdfkit::test::require_value;

Cmap parse(const std::string& text) {
  return CmapParser::parse(pdfkit::platform::as_bytes(text));
}

std::string wrap(const std::string& body) {
  return "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
         "/CMapName /Test def\n/CMapType 2 def\n" +
         body + "\nendcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
}

std::vector<std::pair<std::uint32_t, std::size_t>> codes(const Cmap& map,
                                                         const std::string& bytes) {
  std::vector<std::pair<std::uint32_t, std::size_t>> out;
  const pdfkit::platform::ByteSpan data = pdfkit::platform::as_bytes(bytes);
  std::size_t at = 0;
  while (at < data.size()) {
    const Cmap::Code code = map.next(data, at);
    out.emplace_back(code.value, code.length);
    at += code.length;
  }
  return out;
}

TEST_CASE("codespaces of one to four bytes decide each code's length") {
  const Cmap map = parse(wrap(
      "4 begincodespacerange\n<00> <80>\n<8140> <9FFC>\n<A0A0A0> <A0FFFF>\n<E0000000> <E0FFFFFF>\n"
      "endcodespacerange"));
  using Pair = std::pair<std::uint32_t, std::size_t>;
  CHECK(codes(map, std::string("A\x81\x40\xA0\xA0\xB0\xE0\x01\x02\x03Z", 11)) ==
        std::vector<Pair>{{0x41, 1}, {0x8140, 2}, {0xA0A0B0, 3}, {0xE0010203, 4}, {0x5A, 1}});
  // A byte that no range accepts takes the shortest range's length.
  const Cmap::Code outside = map.next(pdfkit::platform::as_bytes("\xF5"), 0);
  CHECK_FALSE(outside.in_codespace);
  CHECK(outside.length == 1U);
}

TEST_CASE("cidrange and cidchar map codes to CIDs, later entries first") {
  const Cmap map =
      parse(wrap("1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
                 "2 begincidrange\n<0020> <007E> 1\n<0100> <01FF> 500\nendcidrange\n"
                 "1 begincidchar\n<0041> 9999\nendcidchar\n"
                 "1 beginnotdefrange\n<0000> <001F> 1\nendnotdefrange"));
  CHECK(map.cid(0x20) == std::optional<std::uint32_t>(1));
  CHECK(map.cid(0x7E) == std::optional<std::uint32_t>(95));
  CHECK(map.cid(0x41) == std::optional<std::uint32_t>(9999));
  CHECK(map.cid(0x1FF) == std::optional<std::uint32_t>(755));
  CHECK_FALSE(map.cid(0x10).has_value());
  CHECK(map.malformed() == 0U);
}

TEST_CASE("bfchar and bfrange in both forms map codes to text") {
  const Cmap map = parse(
      wrap("1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
           "2 beginbfchar\n<01> <0041>\n<02> <00660069>\nendbfchar\n"
           "2 beginbfrange\n<10> <12> <0061>\n<20> <22> [<0058> <D835DC00> <0059>]\nendbfrange"));
  CHECK(map.unicode(0x01) == std::optional<std::string>("A"));
  CHECK(map.unicode(0x02) == std::optional<std::string>("fi"));
  CHECK(map.unicode(0x10) == std::optional<std::string>("a"));
  CHECK(map.unicode(0x12) == std::optional<std::string>("c"));
  CHECK(map.unicode(0x20) == std::optional<std::string>("X"));
  // A surrogate pair is one character, kept whole: U+1D400.
  CHECK(map.unicode(0x21) == std::optional<std::string>("\xF0\x9D\x90\x80"));
  CHECK(map.unicode(0x22) == std::optional<std::string>("Y"));
  CHECK_FALSE(map.unicode(0x13).has_value());
}

TEST_CASE("a destination incremented past its last byte carries into the byte before") {
  const Cmap map = parse(wrap("1 beginbfrange\n<01> <03> <00FF>\nendbfrange"));
  CHECK(map.unicode_bytes(0x01) == std::optional<std::string>(std::string("\x00\xFF", 2)));
  CHECK(map.unicode_bytes(0x02) == std::optional<std::string>(std::string("\x01\x00", 2)));
  CHECK(map.unicode(0x03) == std::optional<std::string>("\xC4\x81"));
}

TEST_CASE("a range crossing a byte boundary is read as the numeric range") {
  // ISO 32000-1 lets a range vary only in its last byte; producers write
  // whole identity ranges anyway, and pdf.js reads them numerically.
  const Cmap map = parse(wrap("1 beginbfrange\n<00FE> <0102> <0041>\nendbfrange"));
  CHECK(map.unicode(0x00FE) == std::optional<std::string>("A"));
  CHECK(map.unicode(0x0100) == std::optional<std::string>("C"));
  CHECK(map.unicode(0x0102) == std::optional<std::string>("E"));
  CHECK_FALSE(map.unicode(0x0103).has_value());
}

TEST_CASE("a glyph-name destination, usecmap of an identity and /WMode") {
  const Cmap names = parse(wrap("1 beginbfchar\n<41> /Euro\nendbfchar"));
  CHECK(names.unicode(0x41) == std::optional<std::string>("\xE2\x82\xAC"));
  const Cmap vertical =
      parse("/Identity-V usecmap\n/WMode 1 def\n" + wrap("1 begincidchar\n<0041> 5\nendcidchar"));
  CHECK(vertical.vertical());
  CHECK(vertical.cid(0x0041) == std::optional<std::uint32_t>(5));
  CHECK(vertical.cid(0x1234) == std::optional<std::uint32_t>(0x1234));
  const Cmap::Code code = vertical.next(pdfkit::platform::as_bytes("\x12\x34"), 0);
  CHECK(code.value == 0x1234U);
  CHECK(code.length == 2U);
  // A predefined CMap other than the identities has no data here.
  const Cmap other = parse("/UniJIS-UCS2-H usecmap\n");
  CHECK(other.malformed() == 1U);
}

TEST_CASE("malformed entries are skipped and counted, the rest of the map kept") {
  const Cmap map =
      parse(wrap("3 beginbfchar\n<01> <0041>\n(oops) 12\n<03> <0043>\nendbfchar\n"
                 "2 beginbfrange\n<10> <0F> <0061>\n<20> <21> <0062>\nendbfrange\n"
                 "2 begincidrange\n<0000> <00FF> /notanumber\n<0100> <01FF> 7\nendcidrange"));
  CHECK(map.unicode(0x01) == std::optional<std::string>("A"));
  CHECK(map.unicode(0x03) == std::optional<std::string>("C"));
  CHECK(map.unicode(0x21) == std::optional<std::string>("c"));
  CHECK(map.cid(0x100) == std::optional<std::uint32_t>(7));
  CHECK(map.malformed() == 3U);
}

TEST_CASE("a truncated CMap keeps the entries read before the cut") {
  const std::string text = wrap("3 beginbfchar\n<01> <0041>\n<02> <0042>\n<03> <0043>\nendbfchar");
  const Cmap map = parse(text.substr(0, text.find("<03>")));
  CHECK(map.unicode(0x02) == std::optional<std::string>("B"));
  CHECK_FALSE(map.unicode(0x03).has_value());
}

TEST_CASE("UTF-16BE destinations turn into UTF-8, an unpaired surrogate dropped") {
  CHECK(Cmap::utf16be_to_utf8(std::string("\x00\x41\xD8\x3D\xDE\x00", 6)) == "A\xF0\x9F\x98\x80");
  CHECK(Cmap::utf16be_to_utf8(std::string("\xD8\x3D\x00\x41", 4)) == "A");
  // An odd length gains a leading zero byte.
  CHECK(Cmap::utf16be_to_utf8(std::string("\x41", 1)) == "A");
  CHECK(CmapParser::utf8_to_utf16be("A\xF0\x9F\x98\x80") ==
        std::string("\x00\x41\xD8\x3D\xDE\x00", 6));
}

}  // namespace
