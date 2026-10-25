// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of fonts::FontCensus: two fonts switched by Tf, every text-showing
// operator, q and Q restoring the font, a form XObject walked once with the
// font of the content that draws it, a Tf naming no resource, and the counts
// per font and per rung.

#include "fonts/font_census.hpp"

#include "fonts/decoding_rung.hpp"
#include "support/content_builder.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::fonts::DecodingRung;
using pdfkit::fonts::FontCensus;
using pdfkit::test::ContentBuilder;
using pdfkit::test::PdfBuilder;

std::size_t rung(DecodingRung value) {
  return static_cast<std::size_t>(value);
}

TEST_CASE("the census decodes every shown string in the font Tf named") {
  PdfBuilder builder;
  builder.begin_document();
  const auto helvetica = builder.add(
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
  const auto symbol = builder.add(
      "<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding << /Differences [65 /g7] >> >>");
  ContentBuilder form;
  form.begin_text().show("form").end_text();
  const auto form_object =
      builder.add_stream("/Type /XObject /Subtype /Form /BBox [0 0 100 100]", form.text());
  ContentBuilder content;
  content.begin_text().font("F1", 12).show("Hi ");
  content.show_array({std::string("the"), -250.0, std::string("re")});
  content.font("F2", 12).show("AB").end_text();
  content.save().begin_text().font("F1", 10).end_text().restore();
  content.raw("(x) '").raw("1 2 (y) \"");
  content.draw("X1").draw("X1");
  content.begin_text().font("Missing", 9).show("lost").end_text();
  builder.add_page(content.text(), "/Resources << /Font << /F1 " + std::to_string(helvetica) +
                                       " 0 R /F2 " + std::to_string(symbol) +
                                       " 0 R >> /XObject << /X1 " + std::to_string(form_object) +
                                       " 0 R >> >>");
  const auto document = pdfkit::test::open_document(builder.build());
  const FontCensus::Report report = FontCensus::run(*document);

  REQUIRE(report.pages.size() == 1U);
  // After Q the font is F2 again, so ' and " show in Courier, and the form,
  // drawn twice but walked once, inherits Courier too.
  CHECK(report.pages[0] == "Hi thereBxyform");
  REQUIRE(report.fonts.size() == 2U);
  const FontCensus::Font& first = report.fonts[0];
  CHECK(first.name == "Helvetica");
  CHECK(first.kind == "Type1");
  CHECK(first.program == "none");
  CHECK(first.codes == 8U);
  CHECK(first.rungs[rung(DecodingRung::glyph_name)] == 8U);
  const FontCensus::Font& second = report.fonts[1];
  CHECK(second.name == "Courier");
  CHECK(second.codes == 8U);
  CHECK(second.unreadable == 1U);
  CHECK(report.codes == 16U);
  CHECK(report.unreadable == 1U);
  CHECK(report.rungs[rung(DecodingRung::unreadable)] == 1U);
  CHECK(report.missing_fonts == 1U);
}

}  // namespace
