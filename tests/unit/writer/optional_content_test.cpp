// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the optional-content decision of docs/DESIGN.md section 15: group
// names become OC1 to OCn in a stable order, the default configuration's ON
// and OFF state and its /Order are kept (without their labels), the group
// /Usage and /Intent, the configuration's name and the alternate
// configurations are dropped, so an OFF group's content stays hidden; a group
// no kept page uses is dropped with its page.

#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>

namespace {

using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::test::PdfBuilder;

std::string ref(std::uint32_t number) {
  return std::to_string(number) + " 0 R";
}

// Two pages: page 1 draws group A (on) and group B (off), page 2 draws group
// C (on).
std::string fixture() {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t a = builder.add(
      "<< /Type /OCG /Name (Jane Doe layer) /Intent /View /Usage << /CreatorInfo << /Creator "
      "(Secret app) >> >> >>");
  const std::uint32_t b = builder.add("<< /Type /OCG /Name (Hidden notes) >>");
  const std::uint32_t c = builder.add("<< /Type /OCG /Name (Only page two) >>");
  (void)builder.add_page("/OC /a BDC 0 0 m 10 10 l S EMC /OC /b BDC 0 0 m 20 20 l S EMC",
                         "/Resources << /Properties << /a " + ref(a) + " /b " + ref(b) + " >> >>");
  (void)builder.add_page("/OC /c BDC 0 0 m 30 30 l S EMC",
                         "/Resources << /Properties << /c " + ref(c) + " >> >>");
  builder.set(builder.catalog(),
              "<< /Type /Catalog /Pages " + ref(builder.page_tree()) + " /OCProperties << /OCGs [" +
                  ref(c) + " " + ref(a) + " " + ref(b) +
                  "] /D << /Name (Default config) /Creator (Someone) /BaseState /ON /OFF [" +
                  ref(b) + "] /Order [(Label) [" + ref(a) + " " + ref(b) + "] " + ref(c) +
                  "] /AS [] >> /Configs [<< /Name (Alternate) /OFF [" + ref(a) + "] >>] >> >>");
  return builder.build();
}

std::string name_of(pdfkit::syntax::Document& document, const Object& group) {
  const Object resolved = document.resolve(group);
  return resolved.dictionary()->get("Name").string()->bytes;
}

}  // namespace

TEST_CASE("groups are renamed, their state and order kept, the rest dropped") {
  const std::string output = pdfkit::test::identity_write(fixture());
  for (const char* text : {"Jane Doe layer", "Hidden notes", "Only page two", "Secret app",
                           "Default config", "Someone", "Alternate", "Label"}) {
    CHECK_FALSE(pdfkit::test::contains_marker(output, text));
  }
  const auto document = pdfkit::test::reopen(output);
  const Object properties =
      document->resolve_entry(*document->catalog().dictionary(), "OCProperties");
  REQUIRE(properties.dictionary() != nullptr);
  CHECK_FALSE(properties.dictionary()->contains("Configs"));
  const Object::Array& groups = *properties.dictionary()->get("OCGs").array();
  REQUIRE(groups.size() == 3);
  // The /OCGs order of the source: C, A, B.
  CHECK(name_of(*document, groups[0]) == "OC1");
  CHECK(name_of(*document, groups[1]) == "OC2");
  CHECK(name_of(*document, groups[2]) == "OC3");
  for (const Object& group : groups) {
    const Dictionary& dictionary = *document->resolve(group).dictionary();
    CHECK_FALSE(dictionary.contains("Usage"));
    CHECK_FALSE(dictionary.contains("Intent"));
  }
  const Dictionary& defaults = *properties.dictionary()->get("D").dictionary();
  CHECK_FALSE(defaults.contains("Name"));
  CHECK_FALSE(defaults.contains("Creator"));
  CHECK_FALSE(defaults.contains("AS"));
  CHECK(defaults.get("BaseState").is_name("ON"));
  // B, the hidden group, stays OFF.
  const Object::Array& off = *defaults.get("OFF").array();
  REQUIRE(off.size() == 1);
  CHECK(off[0] == groups[2]);
  // The order keeps its nesting and loses its label.
  const Object::Array& order = *defaults.get("Order").array();
  REQUIRE(order.size() == 2);
  REQUIRE(order[0].array() != nullptr);
  CHECK(order[0].array()->size() == 2);
  CHECK(order[1] == groups[0]);
}

TEST_CASE("a group only a removed page uses goes with the page") {
  const std::string output = pdfkit::test::select_write(fixture(), {1});
  const auto document = pdfkit::test::reopen(output);
  const Object properties =
      document->resolve_entry(*document->catalog().dictionary(), "OCProperties");
  REQUIRE(properties.dictionary() != nullptr);
  const Object::Array& groups = *properties.dictionary()->get("OCGs").array();
  REQUIRE(groups.size() == 2);
  CHECK(name_of(*document, groups[0]) == "OC1");
  CHECK(name_of(*document, groups[1]) == "OC2");
  const Object::Array& order =
      *properties.dictionary()->get("D").dictionary()->get("Order").array();
  CHECK(order.size() == 1);
}

TEST_CASE("optional content no kept page uses is dropped whole") {
  const std::string output = pdfkit::test::select_write(fixture(), {2});
  const auto document = pdfkit::test::reopen(output);
  const Object properties =
      document->resolve_entry(*document->catalog().dictionary(), "OCProperties");
  REQUIRE(properties.dictionary() != nullptr);
  CHECK(properties.dictionary()->get("OCGs").array()->size() == 1);
  // Without the groups no page draws, a file whose pages draw none keeps none.
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t lone = builder.add("<< /Type /OCG /Name (Nobody) >>");
  (void)builder.add_page("BT ET");
  builder.set(builder.catalog(), "<< /Type /Catalog /Pages " + ref(builder.page_tree()) +
                                     " /OCProperties << /OCGs [" + ref(lone) + "] /D << /ON [" +
                                     ref(lone) + "] >> >> >>");
  const auto none = pdfkit::test::reopen(pdfkit::test::identity_write(builder.build()));
  CHECK_FALSE(none->catalog().dictionary()->contains("OCProperties"));
}
