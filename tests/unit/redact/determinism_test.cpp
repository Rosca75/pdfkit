// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// One redaction job written twice gives the same bytes and the same report
// (CLAUDE.md section 9): the surgery, the placeholder font's embedding, the
// form rewrite and the rectangles depend on the input and the job alone.

#include "redact/support/redaction.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>

TEST_CASE("one job written twice gives the same bytes") {
  pdfkit::test::TextDocument fixture;
  const std::uint32_t form = fixture.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 600 100] /Resources << /Font << /F1 " +
          std::to_string(fixture.helvetica()) + " 0 R >> >>",
      "BT /F1 10 Tf 10 50 Td (Form text for Zebulon) Tj ET");
  const std::string uses = "/XObject << /Fm1 " + std::to_string(form) + " 0 R >>";
  fixture.page("BT /F1 10 Tf 72 700 Td [(Zeb) -30 (ulon Quarterfield)] TJ ET /Fm1 Do", uses);
  fixture.page("BT /Helvetica-Bold 1 Tf ET /Fm1 Do BT /F1 9 Tf 50 50 Td (zebulon) Tj ET", uses);
  const std::string file = fixture.bytes();
  const std::string job =
      R"({"matches": [{"search": "zebulon", "placeholder": "[NAME_1]"}, {"search": "Quarterfield"}]})";
  const pdfkit::test::Redaction first = pdfkit::test::redact(file, job);
  const pdfkit::test::Redaction second = pdfkit::test::redact(file, job);
  REQUIRE(first.write_status == PDFKIT_OK);
  CHECK(first.pdf == second.pdf);
  CHECK(first.report == second.report);
  CHECK(first.matches == second.matches);
}
