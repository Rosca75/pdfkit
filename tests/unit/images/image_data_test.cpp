// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of pdfkit_image_data through the C ABI, over a document holding a
// JPEG, a Flate image with a soft mask and an image of an unknown filter:
// the JPEG comes back as its stored bytes, or as a PNG with "decode_jpeg";
// the Flate image and its mask as PNGs; an unknown identity and a mask of an
// image without one are invalid_argument; an image that does not decode is
// refused with undecodable_image; an unknown option is invalid_argument. The
// identities are taken from images::ImageCatalogue over the same bytes, as
// pdfkit_images would list them.

#include "pdfkit/pdfkit.h"

#include "images/image_catalogue.hpp"
#include "images/image_fixture.hpp"
#include "support/document_summary.hpp"

#include <doctest.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using pdfkit::test::bytes_of;
using pdfkit::test::ImageFixture;

std::string jpeg() {
  return pdfkit::test::encode_jpeg(2, 2, 1, {0, 85, 170, 255});
}

struct Opened {
  std::string file;
  pdfkit_document* document = nullptr;
  std::map<std::uint32_t, std::string> identity;
  ~Opened() { pdfkit_close(document); }
};

std::string text(const pdfkit_buffer& buffer) {
  return {reinterpret_cast<const char*>(buffer.data), buffer.size};
}

std::unique_ptr<Opened> open(ImageFixture& fixture, const std::vector<std::uint32_t>& numbers) {
  auto opened = std::make_unique<Opened>();
  fixture.open();
  opened->file = fixture.builder().build();
  const pdfkit::images::ImageCatalogue catalogue(fixture.document());
  for (const auto& entry : catalogue.entries()) {
    opened->identity[entry.object_number] = entry.identity;
  }
  for (const std::uint32_t number : numbers) {
    REQUIRE(opened->identity.contains(number));
  }
  pdfkit_buffer out{};
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(opened->file.data()),
                      opened->file.size(), nullptr, &opened->document, &out) == PDFKIT_OK);
  pdfkit_buffer_free(&out);
  return opened;
}

TEST_CASE("pdfkit_image_data answers JPEG bytes, PNGs, masks and its refusals") {
  ImageFixture fixture;
  const std::uint32_t photo = fixture.image(
      "/Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 8 "
      "/Filter /DCTDecode",
      jpeg());
  const std::uint32_t smask = fixture.stream(
      "/Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceGray "
      "/BitsPerComponent 8",
      bytes_of({0, 255}));
  const std::uint32_t flat =
      fixture.image("/Width 2 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask " +
                        std::to_string(smask) + " 0 R",
                    bytes_of({255, 0, 0, 0, 0, 255}));
  const std::uint32_t broken = fixture.image(
      "/Width 2 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /Frobnicate",
      bytes_of({1, 2}));
  const auto opened = open(fixture, {photo, flat, broken});
  pdfkit_document* doc = opened->document;
  pdfkit_buffer out{};

  REQUIRE(pdfkit_image_data(doc, opened->identity[photo].c_str(), nullptr, &out) == PDFKIT_OK);
  CHECK(text(out) == jpeg());
  pdfkit_buffer_free(&out);

  REQUIRE(pdfkit_image_data(doc, opened->identity[photo].c_str(), "{\"decode_jpeg\": true}",
                            &out) == PDFKIT_OK);
  CHECK(text(out).starts_with("\x89PNG"));
  pdfkit_buffer_free(&out);

  REQUIRE(pdfkit_image_data(doc, opened->identity[flat].c_str(), "{}", &out) == PDFKIT_OK);
  CHECK(text(out).starts_with("\x89PNG"));
  const std::string base = text(out);
  pdfkit_buffer_free(&out);
  REQUIRE(pdfkit_image_data(doc, opened->identity[flat].c_str(), "{\"mask\": true}", &out) ==
          PDFKIT_OK);
  CHECK(text(out).starts_with("\x89PNG"));
  CHECK(text(out) != base);
  pdfkit_buffer_free(&out);

  CHECK(pdfkit_image_data(doc, "img:0000000000000000", nullptr, &out) == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(text(out).find("names no image") != std::string::npos);
  pdfkit_buffer_free(&out);

  CHECK(pdfkit_image_data(doc, opened->identity[photo].c_str(), "{\"mask\": true}", &out) ==
        PDFKIT_E_INVALID_ARGUMENT);
  CHECK(text(out).find("has none") != std::string::npos);
  pdfkit_buffer_free(&out);

  CHECK(pdfkit_image_data(doc, opened->identity[broken].c_str(), nullptr, &out) ==
        PDFKIT_E_REFUSED);
  CHECK(text(out).find("\"code\":\"undecodable_image\"") != std::string::npos);
  CHECK(text(out).find("Frobnicate") != std::string::npos);
  pdfkit_buffer_free(&out);

  CHECK(pdfkit_image_data(doc, opened->identity[flat].c_str(), "{\"colour\": 1}", &out) ==
        PDFKIT_E_INVALID_ARGUMENT);
  pdfkit_buffer_free(&out);
  CHECK(pdfkit_image_data(doc, opened->identity[flat].c_str(), "{\"mask\": 1}", &out) ==
        PDFKIT_E_INVALID_ARGUMENT);
  pdfkit_buffer_free(&out);
}

}  // namespace
