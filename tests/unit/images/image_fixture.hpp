// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageFixture builds the small documents the images layer's tests read: a
// one-page document whose page resources name the image XObjects a test adds,
// opened with the library's own reader, and the access to an object of it by
// number. It keeps each test about the image it states rather than about
// writing a file. It also encodes the JPEG fixtures with libjpeg-turbo's own
// compressor.

#ifndef PDFKIT_TESTS_UNIT_IMAGES_IMAGE_FIXTURE_HPP
#define PDFKIT_TESTS_UNIT_IMAGES_IMAGE_FIXTURE_HPP

#include "support/pdf_builder.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace pdfkit::test {

class ImageFixture {
 public:
  ImageFixture();

  // Adds an image XObject (entries inside the dictionary, without /Type,
  // /Subtype and /Length) and names it /Im<n> on the page; returns its number.
  std::uint32_t image(const std::string& entries, const std::string& data);
  // Adds any other object or stream, for masks, functions and lookups.
  std::uint32_t object(const std::string& body) { return builder_.add(body); }
  std::uint32_t stream(const std::string& entries, const std::string& data) {
    return builder_.add_stream(entries, data);
  }

  [[nodiscard]] PdfBuilder& builder() noexcept { return builder_; }

  // Builds and opens the document; the fixture keeps it.
  syntax::Document& open();
  // The document opened.
  [[nodiscard]] syntax::Document& document();

  // The object number of the document opened, resolved.
  [[nodiscard]] syntax::Object get(std::uint32_t number);
  // The stream number of the document opened.
  [[nodiscard]] const syntax::Stream& stream_of(std::uint32_t number);

 private:
  PdfBuilder builder_;
  std::vector<std::uint32_t> images_;
  std::unique_ptr<syntax::Document> document_;
  std::vector<syntax::Object> kept_;
};

// The bytes of a list of byte values, as a std::string for the builder.
[[nodiscard]] std::string bytes_of(std::initializer_list<int> values);

// The JPEG of width x height pixels of components interleaved samples
// (colour space 1 grey, 3 RGB, 4 CMYK; stored as YCCK when ycck), encoded by
// libjpeg-turbo's compressor at quality 95.
[[nodiscard]] std::string encode_jpeg(int width, int height, int components,
                                      const std::vector<std::uint8_t>& pixels, bool ycck = false);

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_UNIT_IMAGES_IMAGE_FIXTURE_HPP
