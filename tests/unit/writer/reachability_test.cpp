// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the reachable-object walk and the renumbering over builder
// fixtures: an unreferenced object is not reached, an object reachable only
// through a dropped reference is not reached, a cycle ends, a missing object
// is counted once, and the numbering is dense, in first-reach order and
// independent of the source's object numbers.

#include "writer/reachability.hpp"

#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "writer/file_writer.hpp"
#include "writer/object_graph.hpp"
#include "writer/renumbering.hpp"

#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::test::PdfBuilder;
using pdfkit::writer::ObjectGraph;
using pdfkit::writer::Reachability;
using pdfkit::writer::Renumbering;

bool reaches(const Reachability& walk, std::uint32_t number) {
  return walk.contains(number);
}

}  // namespace

TEST_CASE("an unreferenced object is not reached; a cycle ends; a missing object is counted") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t page = builder.add_page("BT ET");
  const std::uint32_t orphan = builder.add("<< /Orphan (secret) >>");
  const std::uint32_t a = builder.reserve();
  const std::uint32_t b = builder.reserve();
  builder.set(a, "<< /Next " + std::to_string(b) + " 0 R /Gone 900 0 R /Again 900 0 R >>");
  builder.set(b, "<< /Next " + std::to_string(a) + " 0 R >>");
  builder.set(builder.catalog(), "<< /Type /Catalog /Pages " + std::to_string(builder.page_tree()) +
                                     " 0 R /Ring " + std::to_string(a) + " 0 R >>");
  const auto document = pdfkit::test::open_document(builder.build());
  ObjectGraph graph(*document);
  const Reachability walk(graph);
  CHECK(reaches(walk, builder.catalog()));
  CHECK(reaches(walk, page));
  CHECK(reaches(walk, a));
  CHECK(reaches(walk, b));
  CHECK_FALSE(reaches(walk, orphan));
  CHECK(walk.missing() == 1);
  CHECK(walk.order().front() == builder.catalog());
}

TEST_CASE("an object reachable only through a dropped reference is not reached") {
  PdfBuilder builder;
  builder.begin_document();
  (void)builder.add_page("BT ET");
  const std::uint32_t outline = builder.add("<< /Title (Bookmark) >>");
  builder.set(builder.catalog(), "<< /Type /Catalog /Pages " + std::to_string(builder.page_tree()) +
                                     " 0 R /Outlines " + std::to_string(outline) + " 0 R >>");
  const auto document = pdfkit::test::open_document(builder.build());
  ObjectGraph graph(*document);
  CHECK(Reachability(graph).contains(outline));
  Dictionary catalog = *graph.get(builder.catalog()).dictionary();
  catalog.erase("Outlines");
  graph.set(builder.catalog(), Object::make_dictionary(catalog));
  CHECK_FALSE(Reachability(graph).contains(outline));
}

TEST_CASE("the walk is depth first in stored order; numbers are dense from 1") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t deep = builder.add("<< /Leaf true >>");
  const std::uint32_t second = builder.add("<< /Child " + std::to_string(deep) + " 0 R >>");
  const std::uint32_t first = builder.add("(first)");
  builder.set(builder.catalog(), "<< /Type /Catalog /Pages " + std::to_string(builder.page_tree()) +
                                     " 0 R /A [" + std::to_string(second) + " 0 R " +
                                     std::to_string(first) + " 0 R] >>");
  const std::uint32_t page = builder.add_page("BT ET");
  const auto document = pdfkit::test::open_document(builder.build());
  ObjectGraph graph(*document);
  const Reachability walk(graph);
  const std::vector<std::uint32_t>& order = walk.order();
  REQUIRE(order.size() >= 6);
  CHECK(order[0] == builder.catalog());
  CHECK(order[1] == builder.page_tree());
  CHECK(order[2] == page);
  const auto at = [&](std::uint32_t number) {
    return std::ranges::find(order, number) - order.begin();
  };
  CHECK(at(second) < at(deep));
  CHECK(at(deep) < at(first));
  const Renumbering numbers(order);
  CHECK(numbers.size() == order.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    CHECK(numbers.find(order[i]) == static_cast<std::uint32_t>(i + 1));
  }
  CHECK_FALSE(numbers.find(100000).has_value());
}

TEST_CASE("a shuffled source numbering gives the same output") {
  // The same document with its objects under different numbers in a different
  // order writes the same bytes, because the output numbering follows the
  // walk, not the source.
  const auto make = [](bool shuffled) {
    PdfBuilder builder;
    if (shuffled) {
      for (int i = 0; i < 5; ++i) {
        (void)builder.add("(filler " + std::to_string(i) + ")");
      }
    }
    builder.begin_document();
    const std::uint32_t font = shuffled ? builder.reserve()
                                        : builder.add(
                                              "<< /Type /Font /Subtype /Type1 /BaseFont "
                                              "/Helvetica >>");
    (void)builder.add_page("BT /F1 12 Tf 72 720 Td (Hello) Tj ET",
                           "/Resources << /Font << /F1 " + std::to_string(font) + " 0 R >> >>");
    if (shuffled) {
      builder.set(font, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    }
    return builder.build();
  };
  const auto plain = pdfkit::test::open_document(make(false));
  const auto shuffled = pdfkit::test::open_document(make(true));
  ObjectGraph graph_a(*plain);
  ObjectGraph graph_b(*shuffled);
  CHECK(pdfkit::writer::FileWriter::write(graph_a).bytes ==
        pdfkit::writer::FileWriter::write(graph_b).bytes);
}
