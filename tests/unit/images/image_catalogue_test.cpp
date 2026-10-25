// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::ImageCatalogue: one image reached from three pages and
// from a form is one entry with its pages; two streams of one picture are one
// identity; an image reached only through a tiling pattern or a soft-mask
// group is marked in_pattern; a form that draws itself and a deep tree of
// forms sharing forms are walked once each per page, so the walk ends.

#include "images/image_catalogue.hpp"

#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"

#include <doctest.h>

#include <string>
#include <string_view>

namespace {

using pdfkit::images::ImageCatalogue;
using pdfkit::test::open_document;
using pdfkit::test::PdfBuilder;

const char* const image_entries =
    "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
    "/BitsPerComponent 8";

std::string ref(std::uint32_t number) {
  return std::to_string(number) + " 0 R";
}

TEST_CASE("an image shared by pages and a form is one entry with its pages") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t image = builder.add_stream(image_entries, "A");
  const std::uint32_t copy = builder.add_stream(image_entries, "A");
  const std::uint32_t form = builder.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << /XObject << /I " + ref(copy) +
          " >> >>",
      "/I Do");
  const std::string resources = "/Resources << /XObject << /Im0 " + ref(image) + " >> >>";
  builder.add_page("/Im0 Do", resources);
  builder.add_page("/Im0 Do", resources);
  builder.add_page("/Fm0 Do", "/Resources << /XObject << /Fm0 " + ref(form) + " >> >>");
  builder.add_page("/Im0 Do", resources);
  auto document = open_document(builder.build());
  const ImageCatalogue catalogue(*document);
  REQUIRE(catalogue.entries().size() == 1U);
  const ImageCatalogue::Entry& entry = catalogue.entries().front();
  CHECK(entry.object_number == image);
  CHECK(entry.pages == std::vector<std::uint32_t>{1, 2, 3, 4});
  CHECK_FALSE(entry.in_pattern);
  CHECK(catalogue.find(entry.identity) == &entry);
  CHECK(catalogue.find("img:0000000000000000") == nullptr);
}

TEST_CASE("an image reached only through a pattern or a soft mask is in_pattern") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t tiled = builder.add_stream(image_entries, "P");
  const std::uint32_t masked = builder.add_stream(image_entries, "M");
  const std::uint32_t pattern = builder.add_stream(
      "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 1 1] /XStep 1 "
      "/YStep 1 /Resources << /XObject << /I " +
          ref(tiled) + " >> >>",
      "/I Do");
  const std::uint32_t group = builder.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Group << /S /Transparency >> "
      "/Resources << /XObject << /I " +
          ref(masked) + " >> >>",
      "/I Do");
  builder.add_page("", "/Resources << /Pattern << /P0 " + ref(pattern) +
                           " >> /ExtGState << /G0 << /SMask << /S /Luminosity /G " + ref(group) +
                           " >> >> >> >>");
  auto document = open_document(builder.build());
  const ImageCatalogue catalogue(*document);
  REQUIRE(catalogue.entries().size() == 2U);
  CHECK(catalogue.entries()[0].in_pattern);
  CHECK(catalogue.entries()[1].in_pattern);
}

TEST_CASE("a form drawing itself and a deep tree of shared forms end the walk") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t image = builder.add_stream(image_entries, "D");
  const std::uint32_t loop = builder.reserve();
  builder.set_stream(loop,
                     "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << /XObject << "
                     "/Self " +
                         ref(loop) + " /I " + ref(image) + " >> >>",
                     "/Self Do");
  // Thirty levels, each form naming the next one twice: 2^30 paths, 30 forms.
  std::uint32_t next = loop;
  for (int level = 0; level < 30; ++level) {
    next = builder.add_stream(
        "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << "
        "/XObject << /A " +
            ref(next) + " /B " + ref(next) + " >> >>",
        "/A Do /B Do");
  }
  builder.add_page("/Top Do", "/Resources << /XObject << /Top " + ref(next) + " >> >>");
  auto document = open_document(builder.build());
  const ImageCatalogue catalogue(*document);
  REQUIRE(catalogue.entries().size() == 1U);
  CHECK(catalogue.entries().front().pages == std::vector<std::uint32_t>{1});
}

}  // namespace
