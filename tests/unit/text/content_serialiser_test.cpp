// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of text::ContentSerialiser: the records of a page reserialise to the
// stream (the identity of docs/DESIGN.md section 6.3) over strings written
// every way the syntax allows (escapes, octal, line continuations, CR LF,
// nested parentheses, hexadecimal with white space and an odd final digit,
// two-byte codes split by white space), over several /Contents pieces and
// over forms; a record moved by one byte is caught; an edit replaces one
// operation and copies the rest. Every other text test's fixture page is
// checked the same way by TextDocument::interpret.

#include "text/content_serialiser.hpp"

#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::test::TextDocument;
using pdfkit::text::ContentSerialiser;
using pdfkit::text::PageContent;

std::string as_text(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace

TEST_CASE("records_reserialise_to_the_stream over every way a string is written") {
  TextDocument document;
  document.page(
      "BT /F1 10 Tf 72 700 Td (a\\(b\\)c\\\\d) Tj (\\101\\102\\60x\\0619) Tj\n"
      "(line\\\ncont\\\r\ninued) Tj (cr\r\nlf\rx(nested(deep))) Tj\n"
      "<48 65 6c\n6C 6f> Tj <4142434> Tj [(A) -250 <42> 120.5 (\\n\\t\\q)] TJ\n"
      "/F2 12 Tf <0041 00 42> Tj [<0020> -100 <0041>] TJ (\\000A) Tj 2 1 (\\000B) \" ET");
  const PageContent page = document.interpret();
  CHECK(TextDocument::drawn_text(page).find("a(b)c\\d") == 0U);
  CHECK(TextDocument::drawn_text(page).find("Hello") != std::string::npos);
  CHECK(page.glyphs.size() > 40U);
  // The literal escape \( is two bytes of the stream for one code.
  const std::string bytes = as_text(*page.streams[0].bytes);
  CHECK(bytes.substr(page.glyphs[1].code_start,
                     page.glyphs[1].code_end - page.glyphs[1].code_start) == "\\(");
}

TEST_CASE("pieces of /Contents reserialise as one joined stream") {
  TextDocument document;
  const std::uint32_t first = document.add_stream("", "BT /F1 10 Tf 72 700 Td (Hel");
  const std::uint32_t second = document.add_stream("", "lo) Tj ET");
  // A later /Contents entry replaces the one the fixture writes (last wins).
  document.page("", "",
                "/Contents [" + std::to_string(first) + " 0 R " + std::to_string(second) + " 0 R]");
  const PageContent page = document.interpret();
  // The joining newline is a code 10 inside the string, which WinAnsi leaves
  // unnamed, so it draws an unreadable glyph.
  CHECK(TextDocument::drawn_text(page) == "Hello");
  CHECK(page.streams[0].piece_starts == std::vector<std::size_t>{0, 28});
}

TEST_CASE("a record moved by one byte is a mismatch") {
  TextDocument document;
  document.page("BT /F1 10 Tf (AB) Tj <4344> Tj ET");
  PageContent page = document.interpret();
  REQUIRE(page.glyphs.size() == 4U);
  page.glyphs[1].code_start += 1;
  page.glyphs[1].code_end += 1;
  const ContentSerialiser::Result moved = ContentSerialiser::write(page.streams[0], 0, page.glyphs);
  CHECK_FALSE(moved.mismatches.empty());
  PageContent hex = document.interpret();
  hex.glyphs[2].code_end -= 1;
  CHECK_FALSE(ContentSerialiser::write(hex.streams[0], 0, hex.glyphs).mismatches.empty());
}

TEST_CASE("an edit replaces one operation and copies every other byte") {
  TextDocument document;
  document.page("BT /F1 10 Tf\n(Secret) Tj % note\n(Kept) Tj ET");
  const PageContent page = document.interpret();
  const ContentSerialiser::Result edited =
      ContentSerialiser::write(page.streams[0], 0, page.glyphs, {{2, "[() -3000] TJ"}});
  CHECK(edited.mismatches.empty());
  CHECK(as_text(edited.bytes) == "BT /F1 10 Tf\n[() -3000] TJ % note\n(Kept) Tj ET");
}
