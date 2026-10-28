// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// One occurrence is one place on one page (docs/ABI.md section 4.11): a
// located match inside a form drawn on several pages, or twice on one page,
// removes that draw alone, through a copy of the form (nested forms copied
// along their path), while a search still removes every draw; the
// occurrence form names one occurrence by its index in pdfkit_find's order,
// a rectangle it meets, or the words before and after it, and is refused
// with occurrence_not_found or occurrence_ambiguous (naming the indices
// left) when the narrowings leave none or several; and a located match
// takes every copy of its text drawn at its place, so the self-check's
// location test passes.

#include "redact/support/redaction.hpp"
#include "text/support/text_document.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <string>

namespace {

std::size_t count(const std::string& haystack, const std::string& needle) {
  std::size_t found = 0;
  for (std::size_t at = haystack.find(needle); at != std::string::npos;
       at = haystack.find(needle, at + 1)) {
    ++found;
  }
  return found;
}

std::string occurrence(const std::string& body, const std::string& placeholder = "[NAME]") {
  return R"({"matches": [{"occurrence": {)" + body + R"(}, "placeholder": ")" + placeholder +
         R"("}]})";
}

// A footer form drawn on three pages; nested puts the text in an inner form
// the footer draws.
std::string footer_pages(pdfkit::test::TextDocument& fixture, bool nested) {
  const std::string font = std::to_string(fixture.helvetica());
  const std::string resources = "/Resources << /Font << /F1 " + font + " 0 R >> >> ";
  std::uint32_t form =
      fixture.add_stream("/Type /XObject /Subtype /Form /BBox [0 0 600 100] " + resources,
                         "BT /F1 10 Tf 10 50 Td (Prepared by Zebulon Quarterfield) Tj ET");
  if (nested) {
    form = fixture.add_stream(
        "/Type /XObject /Subtype /Form /BBox [0 0 600 100] /Resources << /XObject << /In " +
            std::to_string(form) + " 0 R >> >> ",
        "q 1 0 0 1 0 0 cm /In Do Q");
  }
  const std::string uses = "/XObject << /Fm1 " + std::to_string(form) + " 0 R >>";
  for (int page = 1; page <= 3; ++page) {
    fixture.page("BT /F1 10 Tf 72 700 Td (Page " + std::to_string(page) +
                     ") Tj ET q 1 0 0 1 72 40 cm /Fm1 Do Q",
                 uses);
  }
  return fixture.bytes();
}

void check_page_two_alone(const pdfkit::test::Redaction& result) {
  REQUIRE(result.match_status == PDFKIT_OK);
  INFO(result.matches);
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.report);
  CHECK(result.report.find("\"status\":\"passed\"") != std::string::npos);
  for (std::uint32_t page = 1; page <= 3; ++page) {
    const std::string text = pdfkit::test::page_text(result.pdf, page);
    INFO("page " << page << ": " << text);
    if (page == 2) {
      CHECK(text.find("Quarterfield") == std::string::npos);
      CHECK(text.find("Zebulon [NAME]") != std::string::npos);
    } else {
      CHECK(text.find("Zebulon Quarterfield") != std::string::npos);
      CHECK(text.find("[NAME]") == std::string::npos);
    }
  }
}

}  // namespace

TEST_CASE("a located match in a footer form drawn on three pages removes one draw") {
  pdfkit::test::TextDocument fixture;
  const std::string file = footer_pages(fixture, false);
  check_page_two_alone(
      pdfkit::test::redact(file, occurrence(R"("text": "Quarterfield", "page": 2, "index": 0)")));
}

TEST_CASE("a located match in a form nested in a shared form is copied along its path") {
  pdfkit::test::TextDocument fixture;
  const std::string file = footer_pages(fixture, true);
  check_page_two_alone(
      pdfkit::test::redact(file, occurrence(R"("text": "Quarterfield", "page": 2, "index": 0)")));
}

TEST_CASE("a located match in a form drawn twice on one page removes that draw") {
  pdfkit::test::TextDocument fixture;
  const std::string font = std::to_string(fixture.helvetica());
  const std::uint32_t form = fixture.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 600 100] /Resources << /Font << /F1 " + font +
          " 0 R >> >> ",
      "BT /F1 10 Tf 10 50 Td (Signed Zebulon) Tj ET");
  fixture.page("q 1 0 0 1 72 600 cm /Fm1 Do Q q 1 0 0 1 72 300 cm /Fm1 Do Q",
               "/XObject << /Fm1 " + std::to_string(form) + " 0 R >>");
  const pdfkit::test::Redaction result = pdfkit::test::redact(
      fixture.bytes(), occurrence(R"("text": "Zebulon", "page": 1, "index": 1)"));
  REQUIRE(result.match_status == PDFKIT_OK);
  REQUIRE(result.write_status == PDFKIT_OK);
  const std::string text = pdfkit::test::page_text(result.pdf);
  INFO(text);
  CHECK(count(text, "Zebulon") == 1);
  CHECK(count(text, "[NAME]") == 1);
}

TEST_CASE("a search still removes every draw of a shared form") {
  pdfkit::test::TextDocument fixture;
  const std::string file = footer_pages(fixture, true);
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, pdfkit::test::search("Quarterfield"));
  REQUIRE(result.write_status == PDFKIT_OK);
  for (std::uint32_t page = 1; page <= 3; ++page) {
    CHECK(pdfkit::test::page_text(result.pdf, page).find("Quarterfield") == std::string::npos);
  }
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Quarterfield"));
}

TEST_CASE("an occurrence is narrowed by index, near, before and after") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "BT /F1 10 Tf 72 700 Td (the EU budget) Tj 0 -20 Td (an EU treaty) Tj 0 -20 Td "
      "(the EU treaty) Tj ET");
  const std::string file = fixture.bytes();
  // The page's text with nothing drawn for the removed occurrence.
  const auto left = [&file](const std::string& body) {
    const pdfkit::test::Redaction result =
        pdfkit::test::redact(file, occurrence(R"("text": "EU", "page": 1, )" + body, ""));
    REQUIRE(result.match_status == PDFKIT_OK);
    REQUIRE(result.write_status == PDFKIT_OK);
    return pdfkit::test::page_text(result.pdf);
  };
  CHECK(left(R"("index": 1)") == "the EU budget\nan treaty\nthe EU treaty");
  // The second line's baseline is at 680 points.
  CHECK(left(R"("near": [60, 678, 600, 690])") == "the EU budget\nan treaty\nthe EU treaty");
  CHECK(left(R"("after": "BUDGET")") == "the budget\nan EU treaty\nthe EU treaty");
  CHECK(left(R"("before": "the", "after": "treaty")") == "the EU budget\nan EU treaty\nthe treaty");
}

TEST_CASE("an occurrence the narrowings leave none or several of is refused by code") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (EU and EU and EU) Tj ET");
  const std::string file = fixture.bytes();
  const pdfkit::test::Redaction none =
      pdfkit::test::redact(file, occurrence(R"("text": "EU", "page": 1, "index": 3)"));
  CHECK(none.match_status == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(none.matches.find("\"code\":\"occurrence_not_found\"") != std::string::npos);
  const pdfkit::test::Redaction several =
      pdfkit::test::redact(file, occurrence(R"("text": "EU", "page": 1, "after": "and")"));
  CHECK(several.match_status == PDFKIT_E_INVALID_ARGUMENT);
  INFO(several.matches);
  CHECK(several.matches.find("\"code\":\"occurrence_ambiguous\"") != std::string::npos);
  CHECK(several.matches.find("leave 2 occurrences, with the indices 0, 1") != std::string::npos);
  const pdfkit::test::Redaction bare =
      pdfkit::test::redact(file, occurrence(R"("text": "EU", "page": 1)"));
  CHECK(bare.match_status == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(bare.matches.find("\"code\":\"invalid_argument\"") != std::string::npos);
}

TEST_CASE("an occurrence resolves to the glyphs find reports and answers as one") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Zebulon and Zebulon) Tj ET");
  const pdfkit::test::Redaction result = pdfkit::test::redact(
      fixture.bytes(), occurrence(R"("text": "zebulon", "page": 1, "index": 1)"));
  REQUIRE(result.match_status == PDFKIT_OK);
  INFO(result.matches);
  CHECK(result.matches.find("\"kind\":\"occurrence\"") != std::string::npos);
  CHECK(result.matches.find("\"glyphs\":[[12,19]]") != std::string::npos);
  CHECK(pdfkit::test::page_text(result.pdf) == "Zebulon and [NAME]");
}

TEST_CASE("a located match takes every copy of its text drawn at its place") {
  pdfkit::test::TextDocument fixture;
  // A second, invisible layer of the word half a point off at another size,
  // which no reader folds into the visible word as a duplicate.
  fixture.page(
      "BT /F1 10 Tf 72 700 Td (Times and Times) Tj ET "
      "BT 3 Tr /F1 9 Tf 72.5 700.5 Td (Times) Tj ET");
  const pdfkit::test::Redaction result = pdfkit::test::redact(
      fixture.bytes(), occurrence(R"("text": "Times", "page": 1, "index": 0)"));
  REQUIRE(result.match_status == PDFKIT_OK);
  INFO(result.matches);
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.report);
  const std::string text = pdfkit::test::page_text(result.pdf);
  CHECK(count(text, "Times") == 1);
  CHECK(text.find("and Times") != std::string::npos);
}
