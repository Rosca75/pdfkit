// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::CrossReference over builder fixtures: the classic table
// with 19-, 20- and 21-byte entries, the stream with each /W shape and a
// predictor, the hybrid file, the /Prev chain where the first entry wins, a
// chain loop, the off-by-one first subsection, and each way the path
// disappoints so the document must reconstruct.

#include "syntax/cross_reference.hpp"

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "support/pdf_builder.hpp"
#include "support/require_value.hpp"
#include "syntax/parser.hpp"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace {

using pdfkit::platform::as_bytes;
using pdfkit::platform::Budget;
using pdfkit::syntax::CrossReference;
using pdfkit::syntax::Parser;
using pdfkit::test::PdfBuilder;
using pdfkit::test::require_value;
using Type = CrossReference::Entry::Type;

struct Read {
  std::optional<CrossReference> table;
  std::string failure;
};

Read read(const std::string& file) {
  Budget budget;
  Read result;
  result.table = CrossReference::read(as_bytes(file), budget, result.failure);
  return result;
}

CrossReference table_of(const std::string& file) {
  Read result = read(file);
  INFO(result.failure);
  return require_value(std::move(result.table));
}

// Whether the entry of number lands on "number g obj" in file.
bool lands(const std::string& file, const CrossReference& table, std::uint32_t number) {
  const CrossReference::Entry* entry = table.find(number);
  if (entry == nullptr || entry->type != Type::offset) {
    return false;
  }
  const auto header = Parser(as_bytes(file)).read_header(entry->offset);
  return header.has_value() && header->first.number == number;
}

PdfBuilder three_objects() {
  PdfBuilder builder;
  builder.begin_document();
  builder.add_page("BT ET");
  return builder;
}

std::int64_t startxref_of(const std::string& file) {
  const std::size_t at = file.rfind("startxref");
  return std::stoll(file.substr(at + 9));
}

TEST_CASE("the classic table with 19-, 20- and 21-byte entries") {
  for (const int bytes : {19, 20, 21}) {
    pdfkit::test::Layout layout;
    layout.entry_bytes = bytes;
    const std::string file = three_objects().build(layout);
    const CrossReference table = table_of(file);
    CHECK(table.entries().size() == 5U);
    CHECK(table.find(0)->type == Type::free);
    for (std::uint32_t number = 1; number <= 4; ++number) {
      CHECK(lands(file, table, number));
    }
    CHECK(require_value(table.trailer().get("Root").reference()).number == 1U);
    CHECK(table.notes().empty());
  }
}

TEST_CASE("the cross-reference stream with computed, wide and default-type widths") {
  for (const std::array<int, 3> widths :
       {std::array<int, 3>{0, 0, 0}, std::array<int, 3>{1, 4, 2}, std::array<int, 3>{2, 8, 1}}) {
    pdfkit::test::Layout layout;
    layout.xref = pdfkit::test::XrefForm::stream;
    layout.xref_widths = widths;
    const std::string file = three_objects().build(layout);
    const CrossReference table = table_of(file);
    for (std::uint32_t number = 1; number <= 4; ++number) {
      CHECK(lands(file, table, number));
    }
    CHECK(require_value(table.trailer().get("Root").reference()).number == 1U);
    CHECK(table.trailer().get("Type").is_name("XRef"));
  }
}

TEST_CASE("a type field of width 0 reads every entry as type 1") {
  pdfkit::test::Layout layout;
  layout.xref = pdfkit::test::XrefForm::stream;
  layout.xref_widths = {0, 4, 2};
  const std::string file = three_objects().build(layout);
  const CrossReference table = table_of(file);
  for (std::uint32_t number = 1; number <= 4; ++number) {
    CHECK(lands(file, table, number));
  }
  // Object 0 is written as offset 0, which names no object and reads as free.
  CHECK(table.find(0)->type == Type::free);
}

TEST_CASE("a cross-reference stream compressed with the PNG Up predictor") {
  pdfkit::test::Layout layout;
  layout.xref = pdfkit::test::XrefForm::stream;
  layout.xref_stream_predictor = true;
  const std::string file = three_objects().build(layout);
  const CrossReference table = table_of(file);
  for (std::uint32_t number = 1; number <= 4; ++number) {
    CHECK(lands(file, table, number));
  }
}

TEST_CASE("compressed entries name their object stream and index") {
  PdfBuilder builder = three_objects();
  pdfkit::test::ObjectOptions packed;
  packed.in_object_stream = true;
  const std::uint32_t first = builder.add("<< /Packed 1 >>", packed);
  const std::uint32_t second = builder.add("(two)", packed);
  pdfkit::test::Layout layout;
  layout.xref = pdfkit::test::XrefForm::stream;
  const CrossReference table = table_of(builder.build(layout));
  CHECK(table.find(first)->type == Type::compressed);
  CHECK(table.find(first)->generation == 0U);
  CHECK(table.find(second)->generation == 1U);
  CHECK(table.find(first)->offset == table.find(second)->offset);
}

TEST_CASE("a hybrid file's /XRefStm fills the numbers its table does not name") {
  PdfBuilder builder = three_objects();
  pdfkit::test::ObjectOptions packed;
  packed.in_object_stream = true;
  const std::uint32_t inside = builder.add("<< /Packed 1 >>", packed);
  pdfkit::test::Layout layout;
  layout.xref = pdfkit::test::XrefForm::hybrid;
  const std::string file = builder.build(layout);
  const CrossReference table = table_of(file);
  CHECK(table.find(inside)->type == Type::compressed);
  CHECK(lands(file, table, 1));
  CHECK(table.trailer().contains("XRefStm"));
}

TEST_CASE("the /Prev chain is followed newest first and the first entry wins") {
  PdfBuilder builder = three_objects();
  builder.new_revision();
  builder.set(builder.catalog(), "<< /Type /Catalog /Pages 2 0 R /Revised true >>");
  for (const auto form : {pdfkit::test::XrefForm::table, pdfkit::test::XrefForm::stream}) {
    pdfkit::test::Layout layout;
    layout.xref = form;
    const std::string file = builder.build(layout);
    const CrossReference table = table_of(file);
    CHECK(table.sections().size() == 2U);
    const CrossReference::Entry* catalog = table.find(builder.catalog());
    REQUIRE(catalog != nullptr);
    // The newer catalog is the one later in the file.
    CHECK(catalog->offset > file.find("/Type /Catalog"));
    CHECK(lands(file, table, builder.catalog()));
    CHECK(lands(file, table, builder.page_tree()));
  }
}

TEST_CASE("a /Prev loop ends without reading a section twice") {
  PdfBuilder builder = three_objects();
  builder.new_revision();
  builder.set(builder.catalog(), "<< /Type /Catalog /Pages 2 0 R >>");
  const std::string plain = builder.build();
  pdfkit::test::Layout layout;
  layout.prev = startxref_of(plain);
  const std::string file = builder.build(layout);
  const CrossReference table = table_of(file);
  CHECK(table.sections().size() == 1U);
}

TEST_CASE("a first subsection labelled 1 that starts with the free head is read from 0") {
  pdfkit::test::Layout layout;
  layout.table_off_by_one = true;
  const std::string file = three_objects().build(layout);
  const CrossReference table = table_of(file);
  for (std::uint32_t number = 1; number <= 4; ++number) {
    CHECK(lands(file, table, number));
  }
  CHECK(table.notes().size() == 1U);
}

TEST_CASE("each way the path disappoints is a reason to reconstruct") {
  pdfkit::test::Layout wrong;
  wrong.startxref = 30;
  CHECK_FALSE(read(three_objects().build(wrong)).failure.empty());
  CHECK_FALSE(read(three_objects().build(wrong)).table.has_value());

  pdfkit::test::Layout beyond;
  beyond.startxref = 999999;
  CHECK_FALSE(read(three_objects().build(beyond)).failure.empty());

  std::string missing = three_objects().build();
  missing.erase(missing.rfind("startxref"));
  CHECK_FALSE(read(missing).failure.empty());

  pdfkit::test::Layout no_table;
  no_table.omit_table = true;
  CHECK_FALSE(read(three_objects().build(no_table)).failure.empty());

  PdfBuilder updated = three_objects();
  updated.new_revision();
  updated.set(updated.catalog(), "<< /Type /Catalog /Pages 2 0 R >>");
  pdfkit::test::Layout nowhere;
  nowhere.prev = 999999;
  CHECK_FALSE(read(updated.build(nowhere)).failure.empty());
}

TEST_CASE("garbage before the header shifts nothing once the span starts at the header") {
  pdfkit::test::Layout layout;
  layout.junk_before_header = std::string(300, 'J');
  const std::string file = three_objects().build(layout);
  const std::string from_header = file.substr(300);
  const CrossReference table = table_of(from_header);
  CHECK(lands(from_header, table, 3));
}

}  // namespace
