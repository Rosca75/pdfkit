// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of redact::ImageTreatmentApplier through pdfkit_redact_images and
// pdfkit_write: one box reaches every draw of an image (two pages and a form)
// because the object is replaced under its own number, its original bytes
// are gone, and the write report's rectangles are the treated draws' device
// boxes; a blur leaves every block constant; a caption is drawn in the
// contrasting colour; a soft mask is opaque under the box; an inline image is
// rewritten in its content stream; a page-space region maps onto the pixels;
// a text redaction and an image treatment share one write; the text and the
// untreated images are as they were.

#include "redact/support/image_redaction.hpp"
#include "redact/support/redaction.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::images::ImageDecoder;
using pdfkit::test::ImageFile;
using pdfkit::test::ImageRedaction;

std::string one(const std::string& treatment) {
  return R"({"treatments": [)" + treatment + "]}";
}

ImageDecoder::Samples only(const std::map<std::uint32_t, ImageDecoder::Samples>& found) {
  REQUIRE(found.size() == 1U);
  return found.begin()->second;
}

}  // namespace

TEST_CASE("one box reaches every draw and the report names each draw's device box") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result = pdfkit::test::treat(
      file.bytes,
      one(R"({"identity": ")" + file.identity.at("shared") + R"(", "treatment": "box"})"));
  INFO(result.answer);
  REQUIRE(result.treatment_status == PDFKIT_OK);
  INFO(result.report);
  REQUIRE(result.write_status == PDFKIT_OK);
  const ImageDecoder::Samples samples = only(pdfkit::test::samples_of(result.pdf, 16, 12));
  for (std::uint32_t y = 0; y < 12; ++y) {
    for (std::uint32_t x = 0; x < 16; ++x) {
      for (std::uint32_t c = 0; c < 3; ++c) {
        REQUIRE(samples.buffer.at(x, y, c) == 0);
      }
    }
  }
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, file.data.at("shared")));
  // The draws: page 1 at (50, 600) 100 by 80, page 2 at (100, 100) 200 by
  // 160, page 3 inside the form at (30, 30) 120 by 90.
  CHECK(result.report.find(
            R"("page":1,"source_page":1,"placeholders":0,"boxes":[[50,600,150,680]])") !=
        std::string::npos);
  CHECK(result.report.find(
            R"("page":2,"source_page":2,"placeholders":0,"boxes":[[100,100,300,260]])") !=
        std::string::npos);
  CHECK(result.report.find(
            R"("page":3,"source_page":3,"placeholders":0,"boxes":[[30,30,150,120]])") !=
        std::string::npos);
  CHECK(result.answer.find(R"("pixels":[[0,0,16,12]])") != std::string::npos);
  // Every draw lists one image, the treated one, and the original identity
  // is gone.
  const auto listed = pdfkit::test::list_images(result.pdf);
  bool found = false;
  for (const auto& entry : listed.find("images")->as_array()) {
    CHECK(entry.find("identity")->as_string() != file.identity.at("shared"));
    if (entry.find("width")->as_int64() == 16 && entry.find("height")->as_int64() == 12) {
      found = true;
      CHECK(entry.find("occurrences")->as_array().size() == 3U);
    }
  }
  CHECK(found);
  // The other images and the text are as they were.
  const ImageDecoder::Samples kept = only(pdfkit::test::samples_of(result.pdf, 8, 8));
  CHECK(kept.buffer.at(3, 3, 0) == static_cast<std::uint8_t>(file.data.at("kept")[27]));
  CHECK(pdfkit::test::contains_marker(result.pdf, file.data.at("kept")));
  CHECK(pdfkit::test::page_text(result.pdf, 1).find("Hello images") != std::string::npos);
}

TEST_CASE("a blur leaves every block of the image constant") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result = pdfkit::test::treat(
      file.bytes,
      one(R"({"identity": ")" + file.identity.at("shared") + R"(", "treatment": "blur"})"));
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(result.answer.find(R"("block":8)") != std::string::npos);
  const ImageDecoder::Samples samples = only(pdfkit::test::samples_of(result.pdf, 16, 12));
  for (std::uint32_t y = 0; y < 12; ++y) {
    for (std::uint32_t x = 0; x < 16; ++x) {
      for (std::uint32_t c = 0; c < 3; ++c) {
        REQUIRE(samples.buffer.at(x, y, c) == samples.buffer.at((x / 8) * 8, (y / 8) * 8, c));
      }
    }
  }
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, file.data.at("shared")));
}

TEST_CASE("a caption is drawn in white on a black box") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result =
      pdfkit::test::treat(file.bytes, one(R"({"identity": ")" + file.identity.at("shared") +
                                          R"(", "treatment": "box", "caption": "X"})"));
  REQUIRE(result.write_status == PDFKIT_OK);
  const ImageDecoder::Samples samples = only(pdfkit::test::samples_of(result.pdf, 16, 12));
  std::size_t white = 0;
  std::size_t black = 0;
  for (std::uint32_t y = 0; y < 12; ++y) {
    for (std::uint32_t x = 0; x < 16; ++x) {
      const auto value = samples.buffer.at(x, y, 0);
      REQUIRE((value == 0 || value == 255));
      (value == 255 ? white : black) += 1;
    }
  }
  CHECK(white > 0U);
  CHECK(black > white);
}

TEST_CASE("a soft mask is opaque under the box") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result =
      pdfkit::test::treat(file.bytes, one(R"({"identity": ")" + file.identity.at("masked") +
                                          R"(", "treatment": "box", "rectangle": [0, 0, 8, 8]})"));
  INFO(result.answer);
  REQUIRE(result.write_status == PDFKIT_OK);
  const auto found = pdfkit::test::samples_of(result.pdf, 16, 16);
  REQUIRE(found.size() == 2U);
  for (const auto& [number, samples] : found) {
    const bool mask = samples.buffer.components() == 1;
    for (std::uint32_t y = 0; y < 16; ++y) {
      for (std::uint32_t x = 0; x < 16; ++x) {
        const bool inside = x < 8 && y < 8;
        if (inside) {
          REQUIRE(samples.buffer.at(x, y, 0) == (mask ? 255 : 0));
        }
      }
    }
  }
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, file.data.at("masked")));
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, file.data.at("mask")));
}

TEST_CASE("an inline image is rewritten inside its content stream") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result = pdfkit::test::treat(
      file.bytes,
      one(R"({"identity": ")" + file.identity.at("inline") + R"(", "treatment": "box"})"));
  INFO(result.report);
  REQUIRE(result.write_status == PDFKIT_OK);
  const std::string contents = pdfkit::test::contents_of(result.pdf);
  CHECK(contents.find("/F [/AHx /Fl]") != std::string::npos);
  CHECK(contents.find("/W 4") != std::string::npos);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, file.data.at("inline")));
  const auto listed = pdfkit::test::list_images(result.pdf);
  bool found = false;
  for (const auto& entry : listed.find("images")->as_array()) {
    if (entry.find("source")->as_string() == "inline") {
      found = true;
      CHECK(entry.find("decodable")->as_bool());
    }
  }
  CHECK(found);
}

TEST_CASE("a page-space region maps onto the pixels of that page's draw") {
  const ImageFile file = pdfkit::test::image_file();
  // Page 2 draws the 16 by 12 image over (100, 100) to (300, 260): the
  // lower-left quarter is columns 0 to 8 and rows 6 to 12.
  const ImageRedaction result = pdfkit::test::treat(
      file.bytes, one(R"({"identity": ")" + file.identity.at("shared") +
                      R"(", "treatment": "region", "space": "page", "page": 2, )"
                      R"("rectangle": [100, 100, 200, 180]})"));
  INFO(result.answer);
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(result.answer.find(R"("pixels":[[0,6,8,12]])") != std::string::npos);
  const ImageDecoder::Samples samples = only(pdfkit::test::samples_of(result.pdf, 16, 12));
  CHECK(samples.buffer.at(0, 11, 0) == 0);
  CHECK(samples.buffer.at(7, 6, 2) == 0);
  CHECK(samples.buffer.at(8, 6, 0) != 0);
  CHECK(samples.buffer.at(0, 5, 0) != 0);
}

TEST_CASE("a text redaction and an image treatment share one write") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result = pdfkit::test::treat(
      file.bytes,
      one(R"({"identity": ")" + file.identity.at("shared") + R"(", "treatment": "box"})"),
      pdfkit::test::search("Hello"));
  INFO(result.report);
  REQUIRE(result.write_status == PDFKIT_OK);
  const std::string text = pdfkit::test::page_text(result.pdf, 1);
  CHECK(text.find("Hello") == std::string::npos);
  CHECK(text.find("[REDACTED]") != std::string::npos);
  CHECK(result.report.find("[50,600,150,680]") != std::string::npos);
  CHECK(result.report.find(R"("status":"passed")") != std::string::npos);
  CHECK(only(pdfkit::test::samples_of(result.pdf, 16, 12)).buffer.at(5, 5, 1) == 0);
}
