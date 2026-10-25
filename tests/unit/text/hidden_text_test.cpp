// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of text::HiddenText, one fixture per class of docs/DESIGN.md chapter
// 8: each hidden word is extracted, flagged with its class, laid out after
// the visible blocks and counted in the page's hidden_text warning; and the
// cases that must stay visible (white text on a dark box, a transparent or
// later non-covering shape) are not flagged.

#include "text/hidden_text.hpp"

#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::test::TextDocument;
using pdfkit::text::HiddenClass;
using pdfkit::text::PageText;

const std::string visible = "BT /F1 10 Tf 72 720 Td (Shown) Tj ET ";
const std::string marker = "(Secret) Tj";

// The page's hidden words, their classes required to include wanted.
void require_hidden(const PageText& page, HiddenClass wanted) {
  CHECK(PageText::text_of(page.blocks) == "Shown");
  REQUIRE(page.hidden_blocks.size() == 1U);
  const auto& word = page.hidden_blocks.front().lines.front().words.front();
  CHECK(word.text == "Secret");
  CHECK((word.hidden & pdfkit::text::hidden_bit(wanted)) != 0);
  CHECK(word.first == 5U);
  for (std::size_t i = word.first; i < word.last; ++i) {
    CHECK(page.content.glyphs[page.order[i]].has(wanted));
  }
  bool warned = false;
  for (const PageText::Warning& warning : page.warnings) {
    if (warning.code == "hidden_text") {
      warned = true;
      CHECK(warning.message.find(std::string(pdfkit::text::hidden_class_name(wanted)) + " 6") !=
            std::string::npos);
    }
  }
  CHECK(warned);
  CHECK(page.text() == "Shown\n\nSecret");
}

PageText read(const std::string& content, const std::string& resources = "",
              const std::string& extra = "") {
  TextDocument document;
  document.page(content, resources, extra);
  return document.read();
}

}  // namespace

TEST_CASE("render modes 3 and 7 are render_invisible") {
  require_hidden(read(visible + "BT /F1 10 Tf 3 Tr 72 600 Td " + marker + " ET"),
                 HiddenClass::render_invisible);
  require_hidden(read(visible + "BT /F1 10 Tf 7 Tr 72 600 Td " + marker + " ET"),
                 HiddenClass::render_invisible);
}

TEST_CASE("white text on nothing is white_fill, on a dark box it is visible") {
  require_hidden(read(visible + "BT 1 g /F1 10 Tf 72 600 Td " + marker + " ET"),
                 HiddenClass::white_fill);
  require_hidden(read(visible + "BT 0 0 0 0 k /F1 10 Tf 72 600 Td " + marker + " ET"),
                 HiddenClass::white_fill);
  const PageText on_dark =
      read(visible + "0 g 60 590 200 30 re f BT 1 1 1 rg /F1 10 Tf 72 600 Td " + marker + " ET");
  CHECK(on_dark.hidden == 0U);
}

TEST_CASE("text outside the clip in force is clipped") {
  require_hidden(read(visible + "q 0 700 612 92 re W n BT /F1 10 Tf 72 600 Td " + marker + " ET Q"),
                 HiddenClass::clipped);
}

TEST_CASE("text outside the media box is off_media, outside only the crop box off_crop") {
  require_hidden(read(visible + "BT /F1 10 Tf 72 900 Td " + marker + " ET"),
                 HiddenClass::off_media);
  require_hidden(
      read(visible + "BT /F1 10 Tf 72 300 Td " + marker + " ET", "", "/CropBox [0 500 612 792]"),
      HiddenClass::off_crop);
}

TEST_CASE("text in a group the default configuration hides is optional_content_off") {
  TextDocument document;
  const std::uint32_t group = document.add("<< /Type /OCG /Name (Layer) >>");
  const std::uint32_t membership =
      document.add("<< /Type /OCMD /OCGs [" + std::to_string(group) + " 0 R] /P /AllOn >>");
  document.catalog_entries("/OCProperties << /OCGs [" + std::to_string(group) +
                           " 0 R] /D << /BaseState /OFF >> >>");
  document.page(visible + "/OC /M BDC BT /F1 10 Tf 72 600 Td " + marker + " ET EMC",
                "/Properties << /M " + std::to_string(membership) + " 0 R >>");
  require_hidden(document.read(), HiddenClass::optional_content_off);
}

TEST_CASE("text a later opaque rectangle or image covers is covered") {
  require_hidden(
      read(visible + "BT /F1 10 Tf 72 600 Td " + marker + " ET 0.9 g 60 590 200 30 re f"),
      HiddenClass::covered);
  TextDocument document;
  const std::uint32_t image = document.add_stream(
      "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8",
      std::string(1, '\x80'));
  document.page(
      visible + "BT /F1 10 Tf 72 600 Td " + marker + " ET q 200 0 0 30 60 590 cm /Im Do Q",
      "/XObject << /Im " + std::to_string(image) + " 0 R >>");
  require_hidden(document.read(), HiddenClass::covered);
  // A transparent box and a box beside the text cover nothing.
  const PageText open = read(visible + "BT /F1 10 Tf 72 600 Td " + marker +
                                 " ET /G gs 60 590 200 30 re f 300 590 100 30 re f",
                             "/ExtGState << /G << /ca 0.5 >> >>");
  CHECK(open.hidden == 0U);
}

TEST_CASE("a copy drawn over the original is duplicate and read once") {
  const PageText page = read(visible + "BT /F1 10 Tf 72 600 Td " + marker + " ET BT /F1 10 Tf " +
                             "72.4 600 Td " + marker + " ET");
  CHECK(PageText::text_of(page.blocks) == "Shown\n\nSecret");
  REQUIRE(page.hidden_blocks.size() == 1U);
  CHECK(page.hidden_blocks.front().lines.front().words.front().hidden ==
        pdfkit::text::hidden_bit(HiddenClass::duplicate));
  CHECK(page.text() == "Shown\n\nSecret");
  CHECK(page.hidden_counts[6] == 6U);
}
