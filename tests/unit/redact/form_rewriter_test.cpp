// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Text inside a form XObject is rewritten inside the form, once (docs/DESIGN.md
// section 12): a form drawn on three pages, redacted by a search that finds
// it on each, leaves the output with one form object, drawn by all three
// pages, whose text lacks the match and draws the placeholder; a glyph range
// on one page reaches every page that draws the form; a form without
// resources of its own gets the placeholder font in the dictionaries of the
// pages that draw it.

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

// A three-page file drawing one form; own_resources gives the form its own
// /Resources naming Helvetica.
std::string three_pages(pdfkit::test::TextDocument& fixture, bool own_resources) {
  const std::string font = std::to_string(fixture.helvetica());
  const std::string resources =
      own_resources ? "/Resources << /Font << /F1 " + font + " 0 R >> >> " : "";
  const std::uint32_t form = fixture.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 600 100] " + resources,
      "BT /F1 10 Tf 10 50 Td (Confidential: Zebulon Quarterfield) Tj ET");
  const std::string uses = "/XObject << /Fm1 " + std::to_string(form) + " 0 R >>";
  for (int page = 1; page <= 3; ++page) {
    fixture.page("BT /F1 10 Tf 72 700 Td (Page " + std::to_string(page) +
                     ") Tj ET q 1 0 0 1 72 600 cm /Fm1 Do Q",
                 uses);
  }
  return fixture.bytes();
}

}  // namespace

TEST_CASE("a form drawn on three pages is rewritten once") {
  pdfkit::test::TextDocument fixture;
  const std::string file = three_pages(fixture, true);
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, pdfkit::test::search("Zebulon Quarterfield"));
  REQUIRE(result.write_status == PDFKIT_OK);
  INFO(result.report);
  CHECK(count(result.pdf, "/Subtype /Form") == 1);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, "Zebulon"));
  for (std::uint32_t page = 1; page <= 3; ++page) {
    const std::string text = pdfkit::test::page_text(result.pdf, page);
    CHECK(text.find("Quarterfield") == std::string::npos);
    CHECK(text.find("[REDACTED]") != std::string::npos);
    CHECK(text.find("Confidential:") != std::string::npos);
  }
  CHECK(result.report.find("\"status\":\"passed\"") != std::string::npos);
}

TEST_CASE("a glyph range inside a form reaches every page that draws it") {
  pdfkit::test::TextDocument fixture;
  const std::string file = three_pages(fixture, false);
  // Find the range and the fingerprint the way a caller does: from the text.
  const pdfkit::test::Redaction probe =
      pdfkit::test::redact(file, pdfkit::test::search("Zebulon Quarterfield"));
  REQUIRE(probe.match_status == PDFKIT_OK);
  const std::size_t at = probe.matches.find("\"fingerprint\":\"");
  REQUIRE(at != std::string::npos);
  const std::string fingerprint = probe.matches.substr(at + 15, 64);
  const std::size_t range = probe.matches.find("\"glyphs\":[");
  REQUIRE(range != std::string::npos);
  const std::size_t comma = probe.matches.find(',', range);
  const std::size_t close = probe.matches.find(']', range);
  const std::string first = probe.matches.substr(range + 10, comma - range - 10);
  const std::string last = probe.matches.substr(comma + 1, close - comma - 1);
  const std::string glyphs = R"({"matches": [{"glyphs": {"page": 1, "first": )" + first +
                             ", \"last\": " + last + R"(}, "fingerprint": ")" + fingerprint +
                             R"(", "placeholder": "[NAME]"}]})";
  const pdfkit::test::Redaction result = pdfkit::test::redact(file, glyphs);
  REQUIRE(result.match_status == PDFKIT_OK);
  REQUIRE(result.write_status == PDFKIT_OK);
  for (std::uint32_t page = 1; page <= 3; ++page) {
    const std::string text = pdfkit::test::page_text(result.pdf, page);
    INFO("page " << page << ": " << text);
    CHECK(text.find("Zebulon") == std::string::npos);
    CHECK(text.find("[NAME]") != std::string::npos);
  }
}

TEST_CASE("a form without resources gets the placeholder font through its pages") {
  pdfkit::test::TextDocument fixture;
  const std::string file = three_pages(fixture, false);
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, pdfkit::test::search("Quarterfield"));
  REQUIRE(result.write_status == PDFKIT_OK);
  for (std::uint32_t page = 1; page <= 3; ++page) {
    const std::string text = pdfkit::test::page_text(result.pdf, page);
    CHECK(text.find("Zebulon [REDACTED]") != std::string::npos);
  }
}
