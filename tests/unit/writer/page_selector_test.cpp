// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of page selection and its leak guarantee (docs/DESIGN.md section 16)
// over the planted four-page fixture: every proper subset of its pages gives
// the right count and order, each kept page carries its inherited attributes,
// the shared resource dictionary is pruned to what the kept pages draw, no
// removed page's marker and no stripped channel's marker survives anywhere in
// the output, every kept marker does, every reference resolves, and the kept
// pages' content is byte-identical to the source's. Two smaller fixtures show
// that a form drawing a form keeps both and that a content stream two kept
// pages share is written once.

#include "writer/page_selector.hpp"

#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "support/pdf_builder_channels.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "writer/output_scan.hpp"
#include "writer/reachability.hpp"

#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::test::PdfBuilder;
using pdfkit::test::PdfBuilderChannels;

constexpr std::uint32_t page_count = 4;

void check_references_resolve(pdfkit::syntax::Document& document) {
  std::vector<Object::Reference> found;
  for (const auto& entry : document.entries()) {
    if (entry.second.type == pdfkit::syntax::CrossReference::Entry::Type::free) {
      continue;
    }
    found.clear();
    pdfkit::writer::Reachability::references(document.object({entry.first, 0}), found);
    for (const Object::Reference reference : found) {
      INFO("object " << entry.first << " names " << reference.number);
      CHECK_FALSE(document.object(reference).is_null());
    }
  }
}

void check_selection(const std::string& source, const std::vector<std::uint32_t>& kept) {
  CAPTURE(kept.size());
  for (const std::uint32_t page : kept) {
    CAPTURE(page);
  }
  const std::string output = pdfkit::test::select_write(source, kept);
  const auto document = pdfkit::test::reopen(output);
  REQUIRE(document->pages().size() == kept.size());
  const auto original = pdfkit::test::open_document(source);
  const std::string all = pdfkit::test::everything(output);
  for (std::uint32_t page = 1; page <= page_count; ++page) {
    const bool is_kept = std::ranges::find(kept, page) != kept.end();
    for (const int row : PdfBuilderChannels::drawn_rows) {
      const std::string marker = PdfBuilderChannels::marker(row, static_cast<int>(page));
      INFO(marker);
      CHECK((all.find(marker) != std::string::npos) == is_kept);
    }
    for (const int row : PdfBuilderChannels::stripped_rows) {
      const std::string marker = PdfBuilderChannels::marker(row, static_cast<int>(page));
      INFO(marker);
      CHECK(all.find(marker) == std::string::npos);
    }
  }
  for (std::size_t i = 0; i < kept.size(); ++i) {
    const auto& page = document->pages()[i];
    // The inherited resources are on the page, pruned to what it draws.
    REQUIRE(page.dictionary.contains("Resources"));
    const Object resources = document->resolve(page.dictionary.get("Resources"));
    const Object xobjects = document->resolve_entry(*resources.dictionary(), "XObject");
    REQUIRE(xobjects.dictionary() != nullptr);
    const std::string n = std::to_string(kept[i]);
    CHECK(xobjects.dictionary()->size() == 3);
    CHECK(xobjects.dictionary()->contains("Fm" + n));
    CHECK(xobjects.dictionary()->contains("Im" + n));
    CHECK(xobjects.dictionary()->contains("Xs" + n));
    CHECK(page.dictionary.contains("MediaBox"));
    CHECK(pdfkit::test::page_text(*document, static_cast<std::uint32_t>(i + 1)) ==
          pdfkit::test::page_text(*original, kept[i]));
  }
  check_references_resolve(*document);
}

}  // namespace

TEST_CASE("every proper subset of the planted fixture is selected without a leak") {
  const std::string source = PdfBuilderChannels::leak_fixture(page_count);
  for (std::uint32_t mask = 1; mask + 1 < (1U << page_count); ++mask) {
    std::vector<std::uint32_t> kept;
    for (std::uint32_t page = 1; page <= page_count; ++page) {
      if ((mask & (1U << (page - 1))) != 0) {
        kept.push_back(page);
      }
    }
    check_selection(source, kept);
  }
}

TEST_CASE("pages are kept in the caller's order") {
  const std::string source = PdfBuilderChannels::leak_fixture(page_count);
  check_selection(source, {4, 1, 3});
  const std::string output = pdfkit::test::select_write(source, {3, 1});
  const auto document = pdfkit::test::reopen(output);
  REQUIRE(document->pages().size() == 2);
  CHECK(pdfkit::test::page_text(*document, 1).find(PdfBuilderChannels::marker(90, 3)) !=
        std::string::npos);
  CHECK(pdfkit::test::page_text(*document, 2).find(PdfBuilderChannels::marker(90, 1)) !=
        std::string::npos);
}

TEST_CASE("the identity write keeps every drawn marker and strips every channel marker") {
  const std::string source = PdfBuilderChannels::leak_fixture(page_count);
  const std::string all = pdfkit::test::everything(pdfkit::test::identity_write(source));
  for (std::uint32_t page = 1; page <= page_count; ++page) {
    for (const int row : PdfBuilderChannels::drawn_rows) {
      CHECK(all.find(PdfBuilderChannels::marker(row, static_cast<int>(page))) != std::string::npos);
    }
    for (const int row : PdfBuilderChannels::stripped_rows) {
      CHECK(all.find(PdfBuilderChannels::marker(row, static_cast<int>(page))) == std::string::npos);
    }
  }
}

TEST_CASE("inherited boxes and rotation are copied onto the kept page") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t font = builder.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  const std::uint32_t contents = builder.add_stream("", "BT /F1 9 Tf (x) Tj ET");
  const std::uint32_t page =
      builder.add("<< /Type /Page /Parent " + std::to_string(builder.page_tree()) +
                  " 0 R /Contents " + std::to_string(contents) + " 0 R >>");
  const std::uint32_t second =
      builder.add("<< /Type /Page /Parent " + std::to_string(builder.page_tree()) +
                  " 0 R /Contents " + std::to_string(contents) + " 0 R >>");
  builder.set(builder.page_tree(),
              "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R " + std::to_string(second) +
                  " 0 R] /Count 2 /MediaBox [0 0 500 700] /CropBox [10 10 400 600] /Rotate 90 "
                  "/Resources << /Font << /F1 " +
                  std::to_string(font) + " 0 R >> >> >>");
  const std::string output = pdfkit::test::select_write(builder.build(), {2});
  const auto document = pdfkit::test::reopen(output);
  REQUIRE(document->pages().size() == 1);
  const auto& kept = document->pages().front();
  CHECK(kept.media_box.right == 500.0);
  CHECK(kept.crop_box.left == 10.0);
  CHECK(kept.rotate == 90);
  CHECK(kept.dictionary.contains("MediaBox"));
  CHECK(kept.dictionary.contains("CropBox"));
  CHECK(kept.dictionary.get("Rotate").integer() == 90);
  const Object fonts =
      document->resolve_entry(*document->resolve(kept.resources).dictionary(), "Font");
  CHECK(fonts.dictionary()->contains("F1"));
}

TEST_CASE("a form drawing a form keeps both; a content stream two kept pages share is one") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t inner =
      builder.add_stream("/Type /XObject /Subtype /Form /BBox [0 0 100 100]",
                         "0 0 m 10 10 l S BT (PDFKITMARK95X01) Tj ET");
  const std::uint32_t unused = builder.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 100 100]", "BT (PDFKITMARK95X02) Tj ET");
  const std::uint32_t outer = builder.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << /XObject << /In " +
          std::to_string(inner) + " 0 R /Unused " + std::to_string(unused) + " 0 R >> >>",
      "/In Do");
  const std::string resources =
      "/Resources << /XObject << /Out " + std::to_string(outer) + " 0 R >> >>";
  const std::uint32_t contents = builder.add_stream("", "/Out Do");
  const std::string page_body = "/Type /Page /Parent " + std::to_string(builder.page_tree()) +
                                " 0 R /MediaBox [0 0 612 792] /Contents " +
                                std::to_string(contents) + " 0 R " + resources;
  const std::uint32_t first = builder.add("<< " + page_body + " >>");
  const std::uint32_t second = builder.add("<< " + page_body + " >>");
  const std::uint32_t third = builder.add_page("BT ET");
  builder.set(builder.page_tree(), "<< /Type /Pages /Kids [" + std::to_string(first) + " 0 R " +
                                       std::to_string(second) + " 0 R " + std::to_string(third) +
                                       " 0 R] /Count 3 >>");
  const std::string output = pdfkit::test::select_write(builder.build(), {1, 2});
  const std::string all = pdfkit::test::everything(output);
  CHECK(all.find("PDFKITMARK95X01") != std::string::npos);
  CHECK(all.find("PDFKITMARK95X02") == std::string::npos);
  const auto document = pdfkit::test::reopen(output);
  REQUIRE(document->pages().size() == 2);
  CHECK(document->pages()[0].contents == document->pages()[1].contents);
  REQUIRE(document->pages()[0].contents.reference().has_value());
}
