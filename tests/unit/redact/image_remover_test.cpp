// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of redact::ImageRemover through pdfkit_redact_images and
// pdfkit_write: an image XObject drawn on two pages and inside a form loses
// every Do, the q and Q around each draw stay balanced, the object is absent
// from the output and its encoded bytes occur nowhere in it; an inline image
// loses its whole BI ... EI and its data; the text and the other images stay.

#include "redact/support/image_redaction.hpp"
#include "redact/support/redaction.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <sstream>
#include <string>

namespace {

using pdfkit::test::ImageFile;
using pdfkit::test::ImageRedaction;

// The count of each of the operators q and Q in content.
std::pair<std::size_t, std::size_t> saves(const std::string& content) {
  std::istringstream tokens(content);
  std::string token;
  std::size_t q = 0;
  std::size_t big_q = 0;
  while (tokens >> token) {
    q += token == "q" ? 1U : 0U;
    big_q += token == "Q" ? 1U : 0U;
  }
  return {q, big_q};
}

std::string removal(const std::string& identity) {
  return R"({"treatments": [{"identity": ")" + identity + R"(", "treatment": "remove"}]})";
}

}  // namespace

TEST_CASE("an image drawn on two pages and in a form loses every draw and leaves the file") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result =
      pdfkit::test::treat(file.bytes, removal(file.identity.at("shared")));
  INFO(result.answer);
  REQUIRE(result.treatment_status == PDFKIT_OK);
  INFO(result.report);
  REQUIRE(result.write_status == PDFKIT_OK);
  const std::string contents = pdfkit::test::contents_of(result.pdf);
  CHECK(contents.find("/ImA Do") == std::string::npos);
  CHECK(contents.find("/ImB Do") != std::string::npos);
  const auto [q, big_q] = saves(contents);
  CHECK(q == big_q);
  CHECK(pdfkit::test::samples_of(result.pdf, 16, 12).empty());
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, file.data.at("shared")));
  const auto listed = pdfkit::test::list_images(result.pdf);
  for (const auto& entry : listed.find("images")->as_array()) {
    CHECK(entry.find("identity")->as_string() != file.identity.at("shared"));
  }
  CHECK(result.report.find(
            R"("treatment":"remove","objects":1,"inline_draws":0,"draws_removed":3)") !=
        std::string::npos);
  CHECK(result.report.find(R"("missing_references":0)") != std::string::npos);
  CHECK(pdfkit::test::page_text(result.pdf, 1).find("Hello images") != std::string::npos);
  // The other images keep their bytes.
  CHECK(pdfkit::test::contains_marker(result.pdf, file.data.at("kept")));
}

TEST_CASE("an inline image loses its whole operation and its data") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result =
      pdfkit::test::treat(file.bytes, removal(file.identity.at("inline")));
  REQUIRE(result.treatment_status == PDFKIT_OK);
  REQUIRE(result.write_status == PDFKIT_OK);
  const std::string contents = pdfkit::test::contents_of(result.pdf);
  CHECK(contents.find("BI") == std::string::npos);
  CHECK(contents.find("/ImA Do") != std::string::npos);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, file.data.at("inline")));
  const auto [q, big_q] = saves(contents);
  CHECK(q == big_q);
}

TEST_CASE("a removal under a page selection reaches the kept pages only") {
  const ImageFile file = pdfkit::test::image_file();
  const ImageRedaction result =
      pdfkit::test::treat(file.bytes, removal(file.identity.at("shared")), "", {3, 2});
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(pdfkit::test::contents_of(result.pdf).find("/ImA Do") == std::string::npos);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, file.data.at("shared")));
  // Source page 2 is output page 2, source page 3 output page 1.
  CHECK(result.report.find(R"("page":1,"source_page":3)") != std::string::npos);
  CHECK(result.report.find(R"("page":2,"source_page":2)") != std::string::npos);
}
