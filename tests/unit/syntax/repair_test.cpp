// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of reconstruction through syntax::Document: every damage of the
// dataset's mutator, built by the fixture writer, either reads as its clean
// source with the repair named, or opens without a repair where the damage is
// one the format tolerates, or is refused with a named code; never a
// different reading in silence.

#include "platform/error.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/document.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::test::Layout;
using pdfkit::test::open_document;
using pdfkit::test::PdfBuilder;
using pdfkit::test::summarise;

PdfBuilder three_pages(bool flate = true) {
  PdfBuilder builder;
  builder.begin_document();
  for (int i = 1; i <= 3; ++i) {
    builder.add_page("BT /F1 12 Tf 72 700 Td (Page " + std::to_string(i) + ") Tj ET", "", flate);
  }
  return builder;
}

std::string clean_summary() {
  auto document = open_document(three_pages().build());
  REQUIRE(document->repairs().empty());
  return summarise(*document);
}

// The damaged file reads as the clean one and names at least one repair.
void check_repaired(const std::string& file) {
  auto document = open_document(file);
  CHECK(summarise(*document) == clean_summary());
  CHECK_FALSE(document->repairs().empty());
}

// The damaged file reads as the clean one with no repair.
void check_tolerated(const std::string& file) {
  auto document = open_document(file);
  CHECK(summarise(*document) == clean_summary());
  CHECK(document->repairs().empty());
}

TEST_CASE("a missing cross-reference table is rebuilt") {
  Layout layout;
  layout.omit_table = true;
  check_repaired(three_pages().build(layout));
  auto document = open_document(three_pages().build(layout));
  CHECK(document->reconstructed());
}

TEST_CASE("a wrong startxref and wrong offsets are rebuilt") {
  Layout wrong_start;
  wrong_start.startxref = 40;
  check_repaired(three_pages().build(wrong_start));
  Layout shifted;
  shifted.offset_shift = 5;
  check_repaired(three_pages().build(shifted));
}

TEST_CASE("a missing %%EOF and a short run of junk before the header are tolerated") {
  Layout no_eof;
  no_eof.omit_eof = true;
  check_tolerated(three_pages().build(no_eof));
  Layout junk;
  junk.junk_before_header = std::string(600, '\x07');
  check_tolerated(three_pages().build(junk));
  auto document = open_document(three_pages().build(junk));
  CHECK(document->header_offset() == 600U);
}

TEST_CASE("junk past the header window is rebuilt") {
  Layout junk;
  junk.junk_before_header = std::string(1500, 'x');
  check_repaired(three_pages().build(junk));
}

TEST_CASE("a short or long /Length, a missing endstream and a missing endobj are recovered") {
  for (const std::int64_t delta : {-3, 4}) {
    PdfBuilder builder = three_pages();
    builder.set_stream(builder.add_stream("", "unused"), "", "BT ET",
                       pdfkit::test::StreamOptions{false, delta, false, false});
    auto document = open_document(builder.build());
    CHECK(summarise(*document) == clean_summary());
    CHECK_FALSE(document->repairs().empty());
    CHECK_FALSE(document->reconstructed());
  }
  PdfBuilder no_endstream = three_pages();
  no_endstream.set_stream(no_endstream.add_stream("", "x"), "", "BT ET",
                          pdfkit::test::StreamOptions{false, 0, false, true});
  auto missing_endstream = open_document(no_endstream.build());
  CHECK(summarise(*missing_endstream) == clean_summary());
  CHECK_FALSE(missing_endstream->repairs().empty());

  PdfBuilder no_endobj = three_pages();
  pdfkit::test::ObjectOptions open_object;
  open_object.omit_endobj = true;
  no_endobj.add("<< /Unused true >>", open_object);
  no_endobj.add("<< /Unused false >>");
  auto missing_endobj = open_document(no_endobj.build());
  CHECK(summarise(*missing_endobj) == clean_summary());
  CHECK_FALSE(missing_endobj->repairs().empty());
}

TEST_CASE("two trailers read as one") {
  Layout layout;
  layout.duplicate_trailer = true;
  check_tolerated(three_pages().build(layout));
}

TEST_CASE("an incremental update is read as its newest revision, and so is its reconstruction") {
  PdfBuilder builder = three_pages();
  builder.new_revision();
  const std::uint32_t contents = builder.add_stream("", "BT (Revised) Tj ET");
  builder.set(builder.pages()[0],
              "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents " +
                  std::to_string(contents) + " 0 R >>");
  auto updated = open_document(builder.build());
  CHECK(updated->repairs().empty());
  CHECK(pdfkit::test::page_text(*updated, 1) == "BT (Revised) Tj ET");
  Layout wrong;
  wrong.startxref = 50;
  auto rebuilt = open_document(builder.build(wrong));
  CHECK(rebuilt->reconstructed());
  // The last occurrence of the page object wins.
  CHECK(pdfkit::test::page_text(*rebuilt, 1) == "BT (Revised) Tj ET");
}

TEST_CASE("a truncated file reads with a repair or is refused naming the damage") {
  const std::string file = three_pages().build();
  for (const std::size_t percent : {55U, 70U, 80U, 95U}) {
    const std::string cut = file.substr(0, file.size() * percent / 100);
    try {
      auto document = open_document(cut);
      CHECK_FALSE(document->repairs().empty());
    } catch (const Error& error) {
      CHECK(error.code() == ErrorCode::no_page);
      CHECK_FALSE(error.fix().empty());
    }
  }
}

TEST_CASE("a linearization hint that names another first page is a repair; a stale one is not") {
  Layout right;
  right.linearized_first_page_delta = 0;
  check_tolerated(three_pages().build(right));
  Layout wrong;
  wrong.linearized_first_page_delta = 1;
  check_repaired(three_pages().build(wrong));
  // An incremental update changes /L's truth, and the hint is then stale by design.
  PdfBuilder updated = three_pages();
  updated.new_revision();
  updated.add("<< /Appended true >>");
  std::string appended = updated.build(wrong);
  // /L keeps the length of the first revision, as an update leaves it.
  const std::size_t length = appended.find("/L ") + 3;
  appended.replace(length, 10, "0000000100");
  auto stale = open_document(appended);
  CHECK(stale->repairs().empty());
}

TEST_CASE("a missing /Root is taken from the catalog object") {
  Layout layout;
  layout.omit_root = true;
  check_repaired(three_pages().build(layout));
}

TEST_CASE("objects in object streams are found by reconstruction") {
  PdfBuilder builder;
  builder.begin_document();
  pdfkit::test::ObjectOptions packed;
  packed.in_object_stream = true;
  const std::uint32_t contents = builder.add_stream("", "BT (Packed) Tj ET");
  const std::uint32_t page =
      builder.add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Contents " +
                      std::to_string(contents) + " 0 R >>",
                  packed);
  builder.set(builder.page_tree(),
              "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R] /Count 1 >>", packed);
  Layout layout;
  layout.xref = pdfkit::test::XrefForm::stream;
  auto clean = open_document(builder.build(layout));
  REQUIRE(clean->pages().size() == 1U);
  CHECK(clean->repairs().empty());
  layout.startxref = 20;
  auto rebuilt = open_document(builder.build(layout));
  CHECK(rebuilt->reconstructed());
  CHECK(summarise(*rebuilt) == summarise(*clean));
}

TEST_CASE("a header and no page is no_page; neither is not_pdf") {
  PdfBuilder builder;
  builder.add("<< /Type /Catalog >>");
  try {
    (void)open_document(builder.build());
    FAIL("the file without pages was opened");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::no_page);
    CHECK_FALSE(error.expected().empty());
  }
  for (const std::string& bytes : {std::string(), std::string("plain text, not a PDF at all")}) {
    try {
      (void)open_document(bytes);
      FAIL("bytes that are not a PDF were opened");
    } catch (const Error& error) {
      CHECK(error.code() == ErrorCode::not_pdf);
      CHECK(error.fix().find("re-export") != std::string::npos);
    }
  }
}

TEST_CASE("a file with no header but a page opens, repaired") {
  std::string file = three_pages().build();
  file.replace(0, 5, "%ZZZ-");
  auto document = open_document(file);
  CHECK(document->pages().size() == 3U);
  CHECK_FALSE(document->repairs().empty());
}

}  // namespace
