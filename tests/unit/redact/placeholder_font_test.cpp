// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of writer::PlaceholderFont (docs/DESIGN.md section 13): the embedded
// font opens through the library's own font loader, every character of the
// alphabet decodes through its ToUnicode to itself with the width its hmtx
// gives, the compiled program's hash equals the one the generator recorded,
// and an output embeds each face once however many placeholders it draws.

#include "writer/placeholder_font.hpp"

#include "fonts/font_loader.hpp"
#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "redact/support/redaction.hpp"
#include "syntax/document.hpp"
#include "syntax/limits.hpp"
#include "text/support/text_document.hpp"
#include "writer/file_writer.hpp"
#include "writer/object_graph.hpp"
#include "writer/page_selector.hpp"

#include <doctest.h>

#include <string>

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

TEST_CASE("the alphabet is the placeholder alphabet") {
  CHECK(PlaceholderFont::in_alphabet("[REDACTED]"));
  CHECK(PlaceholderFont::in_alphabet("[NAME_12]"));
  CHECK_FALSE(PlaceholderFont::in_alphabet("[redacted]"));
  CHECK(PlaceholderFont::outside("[A B]") == ' ');
  CHECK(PlaceholderFont::codes("AB", Face::regular).size() == 4);
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
  const auto document = pdfkit::syntax::Document::open(pdfkit::platform::as_bytes(output),
                                                       pdfkit::syntax::Limits{});
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
    const std::string alphabet(PlaceholderFont::alphabet);
    const std::string codes = PlaceholderFont::codes(alphabet, face);
    const auto decoded = font->decode(pdfkit::platform::as_bytes(codes));
    REQUIRE(decoded.size() == alphabet.size());
    for (std::size_t i = 0; i < alphabet.size(); ++i) {
      INFO("character " << alphabet[i]);
      CHECK(decoded[i].text == std::string(1, alphabet[i]));
      CHECK(decoded[i].byte_length == 2);
      CHECK(font->width(decoded[i].code) ==
            doctest::Approx(PlaceholderFont::width(std::string(1, alphabet[i]), face)));
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
