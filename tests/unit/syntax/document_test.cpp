// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::Document, syntax::PageTree and syntax::Resources: the page
// tree with inheritance, a cycle and the defaults, resource lookup with the
// form fallback, resolution with a missing object and a reference cycle, the
// page ceiling and object limit refusals, the catalog /Version, and a fixture
// holding an external stream, a /URI action, a launch action, /JS and an
// embedded file whose bytes are never decoded, proving none is read.

#include "syntax/document.hpp"

#include "platform/error.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/page.hpp"
#include "syntax/page_tree.hpp"
#include "syntax/resources.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::syntax::Limits;
using pdfkit::syntax::Object;
using pdfkit::syntax::Page;
using pdfkit::syntax::Resources;
using pdfkit::test::open_document;
using pdfkit::test::page_text;
using pdfkit::test::PdfBuilder;

std::string ref(std::uint32_t number) {
  return std::to_string(number) + " 0 R";
}

TEST_CASE("the page tree is walked depth first with inherited attributes") {
  PdfBuilder builder;
  const std::uint32_t catalog = builder.reserve();
  const std::uint32_t root = builder.reserve();
  const std::uint32_t middle = builder.reserve();
  const std::uint32_t font = builder.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  const std::uint32_t contents = builder.add_stream("", "BT ET");
  const std::uint32_t first =
      builder.add("<< /Type /Page /Parent " + ref(root) + " /Contents " + ref(contents) + " >>");
  const std::uint32_t second = builder.add("<< /Type /Page /Parent " + ref(middle) +
                                           " /MediaBox [0 0 200 100] /Rotate -90 >>");
  const std::uint32_t third = builder.add("<< /Type /Page /Parent " + ref(middle) +
                                          " /Rotate 45 /CropBox [50 50 900 900] >>");
  builder.set(catalog, "<< /Type /Catalog /Pages " + ref(root) + " /Version /1.7 >>");
  builder.set(root,
              "<< /Type /Pages /Kids [" + ref(first) + " " + ref(middle) +
                  "] /Count 3 /MediaBox [0 0 500 600] /Rotate 90 /Resources << /Font << /F1 " +
                  ref(font) + " >> >> >>");
  builder.set(middle, "<< /Type /Pages /Parent " + ref(root) + " /Kids [" + ref(second) + " " +
                          ref(third) + "] /Count 2 /CropBox [10 10 400 400] >>");
  builder.set_root(catalog);
  pdfkit::test::Layout layout;
  layout.version = "1.4";
  auto document = open_document(builder.build(layout));
  REQUIRE(document->pages().size() == 3U);
  const Page& one = document->pages()[0];
  CHECK(one.number == 1U);
  CHECK(one.reference.number == first);
  CHECK(one.media_box == Page::Box{0, 0, 500, 600});
  CHECK(one.crop_box == Page::Box{0, 0, 500, 600});
  CHECK(one.rotate == 90);
  CHECK(one.resources.dictionary() != nullptr);
  CHECK(page_text(*document, 1) == "BT ET");
  const Page& two = document->pages()[1];
  CHECK(two.media_box == Page::Box{0, 0, 200, 100});
  // The inherited crop box is clipped to the page's own media box.
  CHECK(two.crop_box == Page::Box{10, 10, 200, 100});
  CHECK(two.rotate == 270);
  const Page& three = document->pages()[2];
  CHECK(three.media_box == Page::Box{0, 0, 500, 600});
  CHECK(three.crop_box == Page::Box{50, 50, 500, 600});
  CHECK(three.rotate == 0);
  CHECK(document->version() == "1.7");
  CHECK(document->repairs().empty());
}

TEST_CASE("a missing media box is US Letter and a reversed box is normalised") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t page = builder.add_page("BT ET");
  builder.set(
      page, "<< /Type /Page /Parent " + ref(builder.page_tree()) + " /CropBox [600 700 10 20] >>");
  auto document = open_document(builder.build());
  CHECK(document->pages()[0].media_box == Page::Box{0, 0, 612, 792});
  CHECK(document->pages()[0].crop_box == Page::Box{10, 20, 600, 700});
}

TEST_CASE("a page tree cycle is skipped with a repair note") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t page = builder.add_page("BT ET");
  builder.set(builder.page_tree(), "<< /Type /Pages /Kids [" + ref(page) + " " +
                                       ref(builder.page_tree()) + "] /Count 1 >>");
  auto document = open_document(builder.build());
  CHECK(document->pages().size() == 1U);
  CHECK(document->repairs().size() == 1U);
}

TEST_CASE("an untyped kid is a node when it has /Kids and a page otherwise") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t leaf =
      builder.add("<< /Contents " + ref(builder.add_stream("", "q Q")) + " >>");
  const std::uint32_t node = builder.add("<< /Kids [" + ref(leaf) + "] >>");
  builder.set(builder.page_tree(), "<< /Type /Pages /Kids [" + ref(node) + "] /Count 1 >>");
  auto document = open_document(builder.build());
  REQUIRE(document->pages().size() == 1U);
  CHECK(document->pages()[0].reference.number == leaf);
}

TEST_CASE("resources: lookup, the form fallback and resource_missing") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t font = builder.add("<< /Type /Font /BaseFont /Courier >>");
  const std::uint32_t page =
      builder.add_page("BT ET", "/Resources << /Font << /F1 " + ref(font) + " >> >>");
  (void)page;
  auto document = open_document(builder.build());
  const Page& first = document->pages()[0];
  const Resources resources(*document, first.resources, first.number);
  const Object found = resources.lookup("Font", "F1");
  REQUIRE(found.dictionary() != nullptr);
  CHECK(found.dictionary()->get("BaseFont").is_name("Courier"));
  // A form without /Resources reads its parent's.
  const pdfkit::syntax::Dictionary bare_form;
  CHECK(resources.for_form(bare_form).lookup("Font", "F1").dictionary() != nullptr);
  CHECK(document->warnings().empty());
  CHECK(resources.lookup("Font", "F9").is_null());
  REQUIRE(document->warnings().size() == 1U);
  CHECK(document->warnings()[0].code == "resource_missing");
  CHECK(document->warnings()[0].page == 1U);
}

TEST_CASE("a missing object resolves to null and a reference cycle ends") {
  PdfBuilder builder;
  builder.begin_document();
  builder.add_page("BT ET");
  const std::uint32_t a = builder.reserve();
  const std::uint32_t b = builder.reserve();
  builder.set(a, ref(b));
  builder.set(b, ref(a));
  auto document = open_document(builder.build());
  CHECK(document->resolve(Object::make_reference({a, 0})).is_null());
  CHECK(document->object({9999, 0}).is_null());
}

TEST_CASE("past the page ceiling the open is refused naming the count and the ceiling") {
  PdfBuilder builder;
  builder.begin_document();
  for (int i = 0; i < 3; ++i) {
    builder.add_page("BT ET");
  }
  Limits limits;
  limits.page_ceiling = 2;
  try {
    (void)open_document(builder.build(), limits);
    FAIL("the page ceiling was not enforced");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::page_ceiling);
    CHECK(error.failed().find("more than 2 pages") != std::string::npos);
    CHECK(error.expected().find('2') != std::string::npos);
    CHECK(error.fix().find("page_ceiling") != std::string::npos);
  }
  limits.page_ceiling = 3;
  CHECK(open_document(builder.build(), limits)->pages().size() == 3U);
}

TEST_CASE("past the object limit the open is refused") {
  PdfBuilder builder;
  builder.begin_document();
  builder.add_page("BT ET");
  Limits limits;
  limits.objects = 3;
  try {
    (void)open_document(builder.build(), limits);
    FAIL("the object limit was not enforced");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
    CHECK(error.fix().find("objects") != std::string::npos);
  }
}

TEST_CASE("external streams, actions, JavaScript and embedded files are never read") {
  const std::string secret = "SECRET-EMBEDDED-BYTES-0123456789";
  std::string payload;
  for (int i = 0; i < 2000; ++i) {
    payload += secret;
  }
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t embedded =
      builder.add_stream("/Type /EmbeddedFile", payload, pdfkit::test::StreamOptions{true});
  const std::uint32_t external = builder.add_stream("/F (C:\\\\secret.txt)", secret);
  const std::uint32_t names = builder.add(
      "<< /EmbeddedFiles << /Names [(a.txt) << /Type /Filespec /F (a.txt) /EF << /F " +
      ref(embedded) +
      " >> >>] >> /JavaScript << /Names [(js) << /S /JavaScript /JS (app.alert(1)) >>] >> >>");
  const std::uint32_t launch = builder.add("<< /S /Launch /F (calc.exe) >>");
  const std::uint32_t uri = builder.add("<< /S /URI /URI (http://example.invalid/) >>");
  builder.set(builder.catalog(), "<< /Type /Catalog /Pages " + ref(builder.page_tree()) +
                                     " /Names " + ref(names) + " /OpenAction " + ref(launch) +
                                     " >>");
  builder.add_page("BT (Visible) Tj ET", "/AA << /O " + ref(uri) + " >>", true);
  auto document = open_document(builder.build());
  CHECK(page_text(*document, 1) == "BT (Visible) Tj ET");
  // Opening and reading the page decoded only the page's content.
  CHECK(document->budget().document_used() < 100U);
  const Object external_stream = document->object({external, 0});
  REQUIRE(external_stream.stream() != nullptr);
  CHECK(document->decode(*external_stream.stream()).bytes.empty());
  CHECK(document->budget().document_used() < 100U);
}

}  // namespace
