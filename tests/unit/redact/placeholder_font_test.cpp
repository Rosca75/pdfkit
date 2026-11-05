// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of writer::PlaceholderFont (docs/DESIGN.md section 13): the embedded
// font opens through the library's own font loader, every character of the
// alphabet (printable ASCII, the Latin-1 Supplement and Latin Extended-A)
// decodes through its ToUnicode to itself with the width its hmtx gives, no
// ToUnicode block holds more than 100 entries, /W agrees with hmtx for every
// glyph it lists, the compiled program's hash equals the one the generator
// recorded, a placeholder is read as UTF-8 and a character outside the
// alphabet is named as U+XXXX, and an output embeds each face once however
// many placeholders it draws.

#include "writer/placeholder_font.hpp"

#include "fonts/font_loader.hpp"
#include "fonts/true_type_tables.hpp"
#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/utf8_sequence.hpp"
#include "redact/support/redaction.hpp"
#include "support/require_value.hpp"
#include "syntax/document.hpp"
#include "syntax/limits.hpp"
#include "text/support/text_document.hpp"
#include "writer/file_writer.hpp"
#include "writer/object_graph.hpp"
#include "writer/page_selector.hpp"

#include <doctest.h>

#include <cmath>
#include <string>
#include <utility>

namespace {

using pdfkit::writer::PlaceholderFont;
using Face = PlaceholderFont::Face;

std::string hex(const std::vector<std::byte>& digest) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (const std::byte value : digest) {
    const auto byte = std::to_integer<unsigned>(value);
    out += digits[byte >> 4U];
    out += digits[byte & 0x0FU];
  }
  return out;
}

std::size_t count(const std::string& haystack, const std::string& needle) {
  std::size_t found = 0;
  for (std::size_t at = haystack.find(needle); at != std::string::npos;
       at = haystack.find(needle, at + 1)) {
    ++found;
  }
  return found;
}

}  // namespace

TEST_CASE("the compiled programs are the bytes the generator hashed") {
  for (const Face face : {Face::regular, Face::bold}) {
    const PlaceholderFont::Metrics& metrics = PlaceholderFont::metrics(face);
    const pdfkit::platform::ByteSpan program(
        reinterpret_cast<const std::byte*>(metrics.program.data()), metrics.program.size());
    CHECK(hex(pdfkit::platform::crypto::Sha2::digest(
              pdfkit::platform::crypto::Sha2::Variant::sha256, program)) == metrics.sha256);
    CHECK(metrics.base_font.size() > 7);
    CHECK(metrics.base_font[6] == '+');
  }
  CHECK(PlaceholderFont::metrics(Face::bold).base_font.ends_with("-Bold"));
  CHECK(PlaceholderFont::metrics(Face::bold).weight == 700);
}

TEST_CASE("the alphabet is printable ASCII, the Latin-1 Supplement and Latin Extended-A") {
  const std::u32string alphabet = PlaceholderFont::alphabet();
  CHECK(alphabet.size() == 319);
  CHECK(alphabet.front() == U' ');
  CHECK(alphabet.back() == U'\u017F');
  CHECK(PlaceholderFont::in_alphabet("[REDACTED]"));
  CHECK(PlaceholderFont::in_alphabet("[NAME_12]"));
  CHECK(PlaceholderFont::in_alphabet("Jérôme Łukasz-O'Brien & Co."));
  CHECK(PlaceholderFont::in_alphabet("*****"));
  CHECK(PlaceholderFont::in_alphabet("[redacted]"));
  CHECK_FALSE(PlaceholderFont::in_alphabet("\u0180"));
  CHECK(PlaceholderFont::outside("ab\u0180c") == "U+0180");
  CHECK(PlaceholderFont::outside("a \u2014 b") == "U+2014");
  CHECK(PlaceholderFont::outside(std::string("a\xC3", 2)) == "an ill-formed UTF-8 sequence");
  CHECK_FALSE(PlaceholderFont::outside("Jérôme").has_value());
  CHECK(PlaceholderFont::codes("AB", Face::regular).size() == 4);
  CHECK(PlaceholderFont::codes("\u00E9\u0141", Face::regular).size() == 4);
}

TEST_CASE("no ToUnicode block holds more than 100 entries") {
  for (const Face face : {Face::regular, Face::bold}) {
    const std::string cmap = PlaceholderFont::to_unicode(face);
    std::size_t entries = 0;
    std::size_t blocks = 0;
    for (std::size_t at = cmap.find(" beginbfchar\n"); at != std::string::npos;
         at = cmap.find(" beginbfchar\n", at + 1)) {
      const std::size_t line = cmap.rfind('\n', at) + 1;
      const std::size_t stated = std::stoul(cmap.substr(line, at - line));
      CHECK(stated <= 100);
      const std::size_t end = cmap.find("endbfchar", at);
      REQUIRE(end != std::string::npos);
      const std::string body = cmap.substr(at, end - at);
      CHECK(count(body, "\n<") == stated);
      entries += stated;
      ++blocks;
    }
    CHECK(entries == 319);
    CHECK(blocks == 4);
  }
}

TEST_CASE("the subset's cmap and hmtx agree with the codes and the widths") {
  for (const Face face : {Face::regular, Face::bold}) {
    const PlaceholderFont::Metrics& metrics = PlaceholderFont::metrics(face);
    pdfkit::platform::Budget budget;
    const auto tables = pdfkit::test::require_value(pdfkit::fonts::TrueTypeTables::parse(
        pdfkit::platform::ByteSpan(reinterpret_cast<const std::byte*>(metrics.program.data()),
                                   metrics.program.size()),
        budget));
    const double units = tables.units_per_em();
    for (const char32_t code_point : PlaceholderFont::alphabet()) {
      std::string text;
      pdfkit::platform::append_utf8(text, code_point);
      const std::string codes = PlaceholderFont::codes(text, face);
      REQUIRE(codes.size() == 2);
      const auto gid = (static_cast<std::uint32_t>(static_cast<unsigned char>(codes[0])) << 8U) |
                       static_cast<unsigned char>(codes[1]);
      INFO("code point " << static_cast<std::uint32_t>(code_point));
      CHECK(tables.lookup(3, 1, code_point) == gid);
      const auto advance = pdfkit::test::require_value(tables.advance(gid));
      CHECK(PlaceholderFont::width(text, face) == std::round(advance * 1000.0 / units));
    }
  }
}

TEST_CASE("the embedded font reads back through the font loader") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (x) Tj ET");
  fixture.open();
  pdfkit::writer::ObjectGraph graph(fixture.document());
  (void)pdfkit::writer::PageSelector::apply(graph, {});
  PlaceholderFont fonts(graph);
  for (const Face face : {Face::regular, Face::bold}) {
    const auto reference = fonts.embed(face);
    CHECK(fonts.embed(face) == reference);
    CHECK(fonts.embedded(face));
    // Reachable from the catalog, so the writer keeps it.
    const auto catalog = graph.get(graph.root());
    pdfkit::syntax::Dictionary entries = *catalog.dictionary();
    entries.set(std::string("Font") + (face == Face::bold ? "B" : "R"),
                pdfkit::syntax::Object::make_reference(reference));
    graph.set(graph.root(), pdfkit::syntax::Object::make_dictionary(std::move(entries)));
  }
  const std::string output = pdfkit::writer::FileWriter::write(graph).bytes;
  const auto document =
      pdfkit::syntax::Document::open(pdfkit::platform::as_bytes(output), pdfkit::syntax::Limits{});
  pdfkit::fonts::FontLoader loader(*document);
  const pdfkit::syntax::Dictionary* catalog = document->catalog().dictionary();
  REQUIRE(catalog != nullptr);
  for (const Face face : {Face::regular, Face::bold}) {
    const auto font =
        loader.load(catalog->get(std::string("Font") + (face == Face::bold ? "B" : "R")));
    REQUIRE(font != nullptr);
    CHECK(font->name() == PlaceholderFont::metrics(face).base_font);
    CHECK(font->is_bold() == (face == Face::bold));
    CHECK(font->is_embedded());
    std::string alphabet;
    for (const char32_t code_point : PlaceholderFont::alphabet()) {
      pdfkit::platform::append_utf8(alphabet, code_point);
    }
    const std::string codes = PlaceholderFont::codes(alphabet, face);
    const auto decoded = font->decode(pdfkit::platform::as_bytes(codes));
    const std::u32string expected = PlaceholderFont::alphabet();
    REQUIRE(decoded.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
      std::string character;
      pdfkit::platform::append_utf8(character, expected[i]);
      INFO("code point " << static_cast<std::uint32_t>(expected[i]));
      CHECK(decoded[i].text == character);
      CHECK(decoded[i].byte_length == 2);
      CHECK(font->width(decoded[i].code) ==
            doctest::Approx(PlaceholderFont::width(character, face)));
    }
    // /W lists every mapped glyph from glyph 1 with the hmtx width the
    // codes are measured by.
    const pdfkit::syntax::Object type0 =
        document->resolve(catalog->get(std::string("Font") + (face == Face::bold ? "B" : "R")));
    const pdfkit::syntax::Object descendant = document->resolve(
        document->resolve(type0.dictionary()->get("DescendantFonts")).array()->front());
    const pdfkit::syntax::Object widths = document->resolve(descendant.dictionary()->get("W"));
    REQUIRE(widths.array() != nullptr);
    REQUIRE(widths.array()->size() == 2);
    CHECK(widths.array()->front().number() == 1.0);
    const pdfkit::syntax::Object::Array& run = *widths.array()->back().array();
    REQUIRE(run.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
      std::string character;
      pdfkit::platform::append_utf8(character, expected[i]);
      const std::string code = PlaceholderFont::codes(character, face);
      CHECK(static_cast<unsigned char>(code[1]) + (static_cast<unsigned char>(code[0]) * 256U) ==
            i + 1);
      CHECK(run[i].number() == PlaceholderFont::width(character, face));
    }
  }
}

TEST_CASE("an output embeds each face once whatever the number of placeholders") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Jane Doe met Jane Doe) Tj ET");
  fixture.page("BT /F1 10 Tf 72 700 Td (and Jane Doe again) Tj ET");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Jane Doe"));
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(count(result.pdf, "/FontFile2") == 1);
  CHECK(count(result.pdf, std::string(PlaceholderFont::metrics(Face::regular).base_font)) == 3);
  const std::string first = pdfkit::test::page_text(result.pdf, 1);
  CHECK(first.find("Jane") == std::string::npos);
  CHECK(first.find("[REDACTED]") != first.rfind("[REDACTED]"));
  CHECK(pdfkit::test::page_text(result.pdf, 2).find("[REDACTED]") != std::string::npos);
}

TEST_CASE("an output that draws no placeholder embeds no placeholder font") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Jane Doe) Tj ET");
  const pdfkit::test::Redaction nothing =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Nobody"));
  REQUIRE(nothing.write_status == PDFKIT_OK);
  CHECK(count(nothing.pdf, "/FontFile2") == 0);
  const pdfkit::test::Redaction empty =
      pdfkit::test::redact(fixture.bytes(), pdfkit::test::search("Jane", ""));
  REQUIRE(empty.write_status == PDFKIT_OK);
  CHECK(count(empty.pdf, "/FontFile2") == 0);
  CHECK(pdfkit::test::page_text(empty.pdf, 1) == "Doe");
}
