// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The fixture documents of the images layer's tests.

#include "images/image_fixture.hpp"

#include "support/document_summary.hpp"

#include <doctest.h>

#include <cstdio>
// jpeglib.h needs FILE from <cstdio> first.
#include <jpeglib.h>

#include <algorithm>
#include <stdexcept>

namespace pdfkit::test {

ImageFixture::ImageFixture() {
  builder_.begin_document();
}

std::uint32_t ImageFixture::image(const std::string& entries, const std::string& data) {
  const std::uint32_t number =
      builder_.add_stream("/Type /XObject /Subtype /Image " + entries, data);
  images_.push_back(number);
  return number;
}

syntax::Document& ImageFixture::open() {
  std::string names;
  for (std::size_t i = 0; i < images_.size(); ++i) {
    names += "/Im" + std::to_string(i) + " " + std::to_string(images_[i]) + " 0 R ";
  }
  builder_.add_page("q 10 0 0 10 0 0 cm /Im0 Do Q", "/Resources << /XObject << " + names + ">> >>");
  document_ = open_document(builder_.build());
  return *document_;
}

syntax::Document& ImageFixture::document() {
  REQUIRE(document_ != nullptr);
  if (document_ == nullptr) {
    throw std::logic_error("ImageFixture: the document is not open");
  }
  return *document_;
}

syntax::Object ImageFixture::get(std::uint32_t number) {
  REQUIRE(document_ != nullptr);
  return document_->object(syntax::Object::Reference{number, 0});
}

const syntax::Stream& ImageFixture::stream_of(std::uint32_t number) {
  kept_.push_back(get(number));
  const syntax::Stream* stream = kept_.back().stream();
  REQUIRE(stream != nullptr);
  if (stream == nullptr) {
    throw std::logic_error("ImageFixture: object " + std::to_string(number) + " is no stream");
  }
  return *stream;
}

std::string bytes_of(std::initializer_list<int> values) {
  std::string out;
  for (const int value : values) {
    out.push_back(static_cast<char>(value));
  }
  return out;
}

std::string encode_jpeg(int width, int height, int components,
                        const std::vector<std::uint8_t>& pixels, bool ycck) {
  jpeg_compress_struct info{};
  jpeg_error_mgr error{};
  info.err = jpeg_std_error(&error);
  jpeg_create_compress(&info);
  // A buffer large enough that libjpeg never reallocates it.
  std::vector<std::uint8_t> buffer(static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height) *
                                       static_cast<std::size_t>(components) * 4 +
                                   4096);
  unsigned char* target = buffer.data();
  unsigned long size = static_cast<unsigned long>(buffer.size());
  jpeg_mem_dest(&info, &target, &size);
  info.image_width = static_cast<JDIMENSION>(width);
  info.image_height = static_cast<JDIMENSION>(height);
  info.input_components = components;
  info.in_color_space = components == 1 ? JCS_GRAYSCALE : components == 3 ? JCS_RGB : JCS_CMYK;
  jpeg_set_defaults(&info);
  if (components == 4) {
    jpeg_set_colorspace(&info, ycck ? JCS_YCCK : JCS_CMYK);
  }
  jpeg_set_quality(&info, 95, TRUE);
  jpeg_start_compress(&info, TRUE);
  const std::size_t stride = static_cast<std::size_t>(width) * static_cast<std::size_t>(components);
  std::vector<std::uint8_t> row(stride);
  while (info.next_scanline < info.image_height) {
    const auto offset = static_cast<std::ptrdiff_t>(info.next_scanline * stride);
    std::copy(pixels.begin() + offset,
              pixels.begin() + offset + static_cast<std::ptrdiff_t>(stride), row.begin());
    JSAMPROW pointer = row.data();
    jpeg_write_scanlines(&info, &pointer, 1);
  }
  jpeg_finish_compress(&info);
  jpeg_destroy_compress(&info);
  REQUIRE(target == buffer.data());
  return {buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size)};
}

}  // namespace pdfkit::test
