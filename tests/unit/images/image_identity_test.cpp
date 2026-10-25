// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::ImageIdentity: the same bytes and parameters stored twice
// share an identity, whatever the key order and number form; a changed
// /Decode, size or byte changes it; an inline image and an XObject of the
// same bytes and parameters share it, the inline abbreviations expanded; it
// survives the file's encryption; a mask is part of its image's identity by
// its own identity, not its object number.

#include "images/image_identity.hpp"

#include "images/image_fixture.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <doctest.h>

#include <string>
#include <string_view>

namespace {

using pdfkit::images::ImageIdentity;
using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::test::ImageFixture;

std::string pixels() {
  return pdfkit::test::bytes_of({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
}

TEST_CASE("one picture stored twice shares one identity") {
  ImageFixture fixture;
  const std::uint32_t a = fixture.image(
      "/Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8", pixels().substr(0, 12));
  const std::uint32_t b = fixture.image(
      "/BitsPerComponent 8.0 /ColorSpace /DeviceRGB /Height 2 /Width 2 /Name /Other", pixels());
  const std::uint32_t decode = fixture.image(
      "/Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Decode [1 0 1 0 1 0]",
      pixels());
  const std::uint32_t bytes = fixture.image(
      "/Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8", pixels().substr(1) + "X");
  auto& document = fixture.open();
  const std::string first = ImageIdentity::of_stream(document, fixture.stream_of(a));
  CHECK(first.starts_with("img:"));
  CHECK(first.size() == 20U);
  CHECK(ImageIdentity::of_stream(document, fixture.stream_of(b)) == first);
  CHECK(ImageIdentity::of_stream(document, fixture.stream_of(decode)) != first);
  CHECK(ImageIdentity::of_stream(document, fixture.stream_of(bytes)) != first);
}

TEST_CASE("an inline image shares the identity of the XObject of its bytes") {
  ImageFixture fixture;
  const std::uint32_t xobject = fixture.image(
      "/Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /ASCIIHexDecode",
      "0102");
  auto& document = fixture.open();
  Dictionary inline_dictionary;
  inline_dictionary.set("W", Object::make_integer(2));
  inline_dictionary.set("H", Object::make_integer(2));
  inline_dictionary.set("CS", Object::make_name("RGB"));
  inline_dictionary.set("BPC", Object::make_integer(8));
  inline_dictionary.set("F", Object::make_name("AHx"));
  CHECK(ImageIdentity::of_inline(document, inline_dictionary, pdfkit::platform::as_bytes("0102")) ==
        ImageIdentity::of_stream(document, fixture.stream_of(xobject)));
}

TEST_CASE("an identity survives encryption and renumbering") {
  const auto identity_in = [](bool encrypted, int padding) {
    pdfkit::test::PdfBuilder builder;
    builder.begin_document();
    for (int i = 0; i < padding; ++i) {
      builder.add("<< /Padding true >>");
    }
    const std::uint32_t image = builder.add_stream(
        "/Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB "
        "/BitsPerComponent 8",
        pixels());
    builder.add_page("", "/Resources << /XObject << /Im0 " + std::to_string(image) + " 0 R >> >>");
    if (encrypted) {
      pdfkit::test::PdfEncryptor::Spec spec;
      builder.encrypt(spec);
    }
    auto document = pdfkit::test::open_document(builder.build());
    const Object stream = document->object(Object::Reference{image, 0});
    return ImageIdentity::of_stream(*document, *stream.stream());
  };
  const std::string plain = identity_in(false, 0);
  CHECK(identity_in(false, 3) == plain);
  CHECK(identity_in(true, 0) == plain);
}

TEST_CASE("a mask counts by its own identity") {
  const auto identity_with_mask = [](int padding, const std::string& mask_bytes) {
    ImageFixture fixture;
    for (int i = 0; i < padding; ++i) {
      (void)fixture.object("<< /Padding true >>");
    }
    const std::uint32_t mask = fixture.stream(
        "/Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceGray "
        "/BitsPerComponent 8",
        mask_bytes);
    const std::uint32_t image =
        fixture.image("/Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask " +
                          std::to_string(mask) + " 0 R",
                      pixels());
    auto& document = fixture.open();
    return ImageIdentity::of_stream(document, fixture.stream_of(image));
  };
  const std::string base = identity_with_mask(0, "abcd");
  CHECK(identity_with_mask(2, "abcd") == base);
  CHECK(identity_with_mask(0, "abce") != base);
}

TEST_CASE("the inline abbreviations of keys expand") {
  CHECK(ImageIdentity::full_key("W") == "Width");
  CHECK(ImageIdentity::full_key("BPC") == "BitsPerComponent");
  CHECK(ImageIdentity::full_key("IM") == "ImageMask");
  CHECK(ImageIdentity::full_key("DP") == "DecodeParms");
  CHECK(ImageIdentity::full_key("Width") == "Width");
}

}  // namespace
