// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::ImageListing and pdfkit_images: one image drawn on three
// pages and once more through a form is one identity with four occurrences,
// each with its page, its ordinal in the page's draw order (the form's draw
// counted where the form is drawn) and its form path; an inline image and an
// XObject of the same bytes and parameters are one entry; an image reached
// only through a pattern the page sets is listed with its page and no
// occurrence; the JSON of pdfkit_images lists them with their warnings, a
// pages option narrows the occurrences, and a page out of range is refused.

#include "images/image_listing.hpp"

#include "pdfkit/pdfkit.h"

#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"

#include <doctest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using pdfkit::images::ImageListing;
using pdfkit::test::open_document;
using pdfkit::test::PdfBuilder;

constexpr const char* gray_entries =
    "/Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceGray "
    "/BitsPerComponent 8";

std::string ref(std::uint32_t number) {
  return std::to_string(number) + " 0 R";
}

struct Fixture {
  std::string file;
  std::uint32_t image = 0;
  std::uint32_t form = 0;
};

// Three pages draw the image; the second also draws a form that draws it
// again, and an inline copy of the same picture; the third sets a pattern
// whose own image is drawn nowhere else.
Fixture build() {
  PdfBuilder builder;
  builder.begin_document();
  Fixture fixture;
  fixture.image = builder.add_stream(gray_entries, "AB");
  fixture.form = builder.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << /XObject << /I " +
          ref(fixture.image) + " >> >>",
      "q /I Do Q");
  const std::uint32_t tile = builder.add_stream(gray_entries, "PQ");
  const std::uint32_t pattern = builder.add_stream(
      "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 1 1] /XStep 1 "
      "/YStep 1 /Resources << /XObject << /T " +
          ref(tile) + " >> >>",
      "/T Do");
  const std::string resources = "/Resources << /XObject << /Im " + ref(fixture.image) + " /Fm " +
                                ref(fixture.form) + " >> /Pattern << /P0 " + ref(pattern) +
                                " >> >>";
  builder.add_page("q 10 0 0 10 0 0 cm /Im Do Q", resources);
  builder.add_page(
      "q 10 0 0 10 0 0 cm /Im Do Q /Fm Do q 20 0 0 20 5 5 cm BI /W 2 /H 1 /CS /G /BPC 8 ID AB EI "
      "Q",
      resources);
  builder.add_page("/Pattern cs /P0 scn 0 0 10 10 re f q 10 0 0 10 0 0 cm /Im Do Q", resources);
  fixture.file = builder.build();
  return fixture;
}

TEST_CASE("one image drawn on three pages and in a form is one entry of four draws") {
  const Fixture fixture = build();
  auto document = open_document(fixture.file);
  ImageListing listing(*document);
  listing.read({3, 1, 2});
  REQUIRE(listing.entries().size() == 2U);
  const ImageListing::Entry& drawn = listing.entries().front().occurrences.empty()
                                         ? listing.entries().back()
                                         : listing.entries().front();
  CHECK(drawn.object_number == fixture.image);
  // Page 1 once, page 2 three times (the Do, the form, the inline copy),
  // page 3 once.
  REQUIRE(drawn.occurrences.size() == 5U);
  CHECK(drawn.occurrences[0].page == 1U);
  CHECK(drawn.occurrences[0].ordinal == 0U);
  CHECK(drawn.occurrences[1].page == 2U);
  CHECK(drawn.occurrences[1].ordinal == 0U);
  CHECK(drawn.occurrences[1].form_path.empty());
  CHECK(drawn.occurrences[2].page == 2U);
  CHECK(drawn.occurrences[2].ordinal == 1U);
  CHECK(drawn.occurrences[2].form_path == std::vector<std::uint32_t>{fixture.form});
  CHECK(drawn.occurrences[3].page == 2U);
  CHECK(drawn.occurrences[3].ordinal == 2U);
  CHECK(drawn.occurrences[3].box.left == doctest::Approx(5.0));
  CHECK(drawn.occurrences[3].box.right == doctest::Approx(25.0));
  CHECK(drawn.occurrences[4].page == 3U);
  CHECK(drawn.occurrences[4].ordinal == 0U);
  CHECK(drawn.pattern_pages.empty());

  const ImageListing::Entry& tiled = listing.entries().front().occurrences.empty()
                                         ? listing.entries().front()
                                         : listing.entries().back();
  CHECK(tiled.occurrences.empty());
  CHECK(tiled.pattern_pages == std::vector<std::uint32_t>{3});

  ImageListing::Entry* found = listing.find(drawn.identity);
  REQUIRE(found != nullptr);
  listing.probe(*found);
  CHECK(found->probed);
  CHECK(found->fault.empty());
}

struct Opened {
  pdfkit_document* document = nullptr;
  Opened(const Opened&) = delete;
  Opened& operator=(const Opened&) = delete;
  Opened(Opened&&) = delete;
  Opened& operator=(Opened&&) = delete;
  explicit Opened(const std::string& file) {
    pdfkit_buffer out{nullptr, 0, nullptr};
    REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                        &document, &out) == PDFKIT_OK);
    pdfkit_buffer_free(&out);
  }
  ~Opened() { pdfkit_close(document); }
};

std::string call(pdfkit_document* document, const char* options, pdfkit_status expected) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  CHECK(pdfkit_images(document, options, &out) == expected);
  std::string text(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  return text;
}

TEST_CASE("pdfkit_images lists images, occurrences and warnings as JSON") {
  const Fixture fixture = build();
  const Opened opened(fixture.file);
  const std::string all = call(opened.document, nullptr, PDFKIT_OK);
  CHECK(all.starts_with(R"({"pages":[1,2,3],"count":2,"images":[{"identity":"img:)"));
  CHECK(all.find(R"("source":"xobject","object":)" + std::to_string(fixture.image)) !=
        std::string::npos);
  CHECK(all.find(R"("occurrences":[{"page":1,"ordinal":0,"box":[0.00,0.00,10.00,10.00])") !=
        std::string::npos);
  CHECK(all.find(R"("form_path":[)" + std::to_string(fixture.form) + "]") != std::string::npos);
  CHECK(all.find(R"("warnings":["image_in_pattern"])") != std::string::npos);
  CHECK(all.find(R"("pattern_pages":[3])") != std::string::npos);
  CHECK(all.find(R"({"code":"image_in_pattern")") != std::string::npos);
  CHECK(all.find(R"("decodable":true)") != std::string::npos);

  const std::string second = call(opened.document, R"({"pages": [2]})", PDFKIT_OK);
  CHECK(second.find(R"("page":1,)") == std::string::npos);
  CHECK(second.find(R"("page":2,"ordinal":2)") != std::string::npos);

  CHECK(call(opened.document, R"({"pages": [4]})", PDFKIT_E_INVALID_ARGUMENT)
            .find("of a document of 3 pages") != std::string::npos);
  CHECK(call(opened.document, R"({"pages": [1, 1]})", PDFKIT_E_INVALID_ARGUMENT).find("twice") !=
        std::string::npos);
  CHECK(call(opened.document, R"({"glyphs": true})", PDFKIT_E_INVALID_ARGUMENT)
            .find("unknown option") != std::string::npos);
}

TEST_CASE("an undecodable image is listed with its warning") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t broken = builder.add_stream(
      "/Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8 /Filter /Frobnicate",
      "AB");
  builder.add_page("/X Do", "/Resources << /XObject << /X " + ref(broken) + " >> >>");
  const Opened opened(builder.build());
  const std::string text = call(opened.document, nullptr, PDFKIT_OK);
  CHECK(text.find(R"("decodable":false,"warnings":["undecodable_image"])") != std::string::npos);
  CHECK(text.find(R"({"code":"undecodable_image","message":"the image img:)") != std::string::npos);
}

}  // namespace
