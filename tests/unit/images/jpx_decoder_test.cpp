// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::JpxDecoder over codestreams encoded in the test by
// OpenJPEG's own encoder (lossless, so the samples come back exactly): a grey
// J2K codestream, an RGB JP2 file, a four-component codestream whose last
// component is read as the /SMaskInData alpha, a 12-bit codestream widened to
// 16-bit samples, data that is neither a codestream nor a JP2 file refused as
// a fault, and an area past the per-stream limit refused with limit_exceeded.

#include "images/jpx_decoder.hpp"

#include "platform/budget.hpp"
#include "platform/error.hpp"

#include <doctest.h>
#include <openjpeg.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using pdfkit::images::JpxDecoder;
using pdfkit::platform::Budget;

struct Sink {
  std::vector<std::uint8_t> bytes;
  std::size_t position = 0;
};

OPJ_SIZE_T write_sink(void* buffer, OPJ_SIZE_T size, void* user) {
  auto* sink = static_cast<Sink*>(user);
  if (sink->position + size > sink->bytes.size()) {
    sink->bytes.resize(sink->position + size);
  }
  std::memcpy(sink->bytes.data() + sink->position, buffer, size);
  sink->position += size;
  return size;
}

OPJ_OFF_T skip_sink(OPJ_OFF_T count, void* user) {
  auto* sink = static_cast<Sink*>(user);
  sink->position += static_cast<std::size_t>(count);
  if (sink->position > sink->bytes.size()) {
    sink->bytes.resize(sink->position);
  }
  return count;
}

OPJ_BOOL seek_sink(OPJ_OFF_T position, void* user) {
  auto* sink = static_cast<Sink*>(user);
  sink->position = static_cast<std::size_t>(position);
  if (sink->position > sink->bytes.size()) {
    sink->bytes.resize(sink->position);
  }
  return OPJ_TRUE;
}

// The lossless encoding of components planes of width x height samples of
// precision bits, as a J2K codestream or a JP2 file.
std::vector<std::uint8_t> encode(OPJ_CODEC_FORMAT format, int width, int height, int components,
                                 int precision, const std::vector<std::vector<int>>& planes,
                                 OPJ_COLOR_SPACE space) {
  std::vector<opj_image_cmptparm_t> parameters(static_cast<std::size_t>(components));
  for (opj_image_cmptparm_t& component : parameters) {
    std::memset(&component, 0, sizeof(component));
    component.dx = 1;
    component.dy = 1;
    component.w = static_cast<OPJ_UINT32>(width);
    component.h = static_cast<OPJ_UINT32>(height);
    component.prec = static_cast<OPJ_UINT32>(precision);
    component.sgnd = 0;
  }
  opj_image_t* image =
      opj_image_create(static_cast<OPJ_UINT32>(components), parameters.data(), space);
  REQUIRE(image != nullptr);
  image->x0 = 0;
  image->y0 = 0;
  image->x1 = static_cast<OPJ_UINT32>(width);
  image->y1 = static_cast<OPJ_UINT32>(height);
  for (int c = 0; c < components; ++c) {
    for (int i = 0; i < width * height; ++i) {
      image->comps[c].data[i] = planes[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
    }
  }
  opj_cparameters_t settings;
  opj_set_default_encoder_parameters(&settings);
  settings.tcp_numlayers = 1;
  settings.tcp_rates[0] = 0;
  settings.cp_disto_alloc = 1;
  settings.numresolution = 2;
  opj_codec_t* codec = opj_create_compress(format);
  REQUIRE(opj_setup_encoder(codec, &settings, image) == OPJ_TRUE);
  Sink sink;
  opj_stream_t* stream = opj_stream_create(1U << 16U, OPJ_FALSE);
  opj_stream_set_write_function(stream, write_sink);
  opj_stream_set_skip_function(stream, skip_sink);
  opj_stream_set_seek_function(stream, seek_sink);
  opj_stream_set_user_data(stream, &sink, nullptr);
  const bool encoded = opj_start_compress(codec, image, stream) == OPJ_TRUE &&
                       opj_encode(codec, stream) == OPJ_TRUE &&
                       opj_end_compress(codec, stream) == OPJ_TRUE;
  opj_stream_destroy(stream);
  opj_destroy_codec(codec);
  opj_image_destroy(image);
  REQUIRE(encoded);
  return sink.bytes;
}

std::vector<std::vector<int>> planes(int width, int height, int components, int precision) {
  std::vector<std::vector<int>> out(static_cast<std::size_t>(components));
  const int max = static_cast<int>((1U << static_cast<unsigned>(precision)) - 1U);
  for (int c = 0; c < components; ++c) {
    for (int i = 0; i < width * height; ++i) {
      out[static_cast<std::size_t>(c)].push_back((i * 37 + c * 91) % (max + 1));
    }
  }
  return out;
}

JpxDecoder::Result decode(const std::vector<std::uint8_t>& data, JpxDecoder::Options options = {}) {
  Budget budget;
  return JpxDecoder::decode(std::as_bytes(std::span(data)), options, budget, "object 3 0");
}

TEST_CASE("a grey J2K codestream decodes exactly") {
  const auto source = planes(8, 8, 1, 8);
  const auto data = encode(OPJ_CODEC_J2K, 8, 8, 1, 8, source, OPJ_CLRSPC_GRAY);
  CHECK(data[0] == 0xFF);
  CHECK(data[1] == 0x4F);
  const JpxDecoder::Result result = decode(data);
  REQUIRE(result.fault.empty());
  CHECK(result.width == 8U);
  CHECK(result.components == 1U);
  CHECK(result.bits == 8U);
  for (std::size_t i = 0; i < 64; ++i) {
    CHECK(result.samples[i] == source[0][i]);
  }
}

TEST_CASE("an RGB JP2 file decodes to interleaved samples") {
  const auto source = planes(8, 4, 3, 8);
  const auto data = encode(OPJ_CODEC_JP2, 8, 4, 3, 8, source, OPJ_CLRSPC_SRGB);
  const JpxDecoder::Result result = decode(data);
  REQUIRE(result.fault.empty());
  CHECK(result.components == 3U);
  CHECK(result.samples.size() == 8U * 4U * 3U);
  for (std::size_t i = 0; i < 32; ++i) {
    for (std::size_t c = 0; c < 3; ++c) {
      CHECK(result.samples[i * 3 + c] == source[c][i]);
    }
  }
}

TEST_CASE("the component after the colour is the in-data alpha") {
  const auto source = planes(4, 4, 4, 8);
  const auto data = encode(OPJ_CODEC_J2K, 4, 4, 4, 8, source, OPJ_CLRSPC_SRGB);
  JpxDecoder::Options options;
  options.components = 3;
  options.alpha = true;
  const JpxDecoder::Result result = decode(data, options);
  REQUIRE(result.fault.empty());
  CHECK(result.components == 3U);
  REQUIRE(result.alpha.size() == 16U);
  CHECK(result.alpha[5] == source[3][5]);
}

TEST_CASE("a 12-bit codestream widens to 16-bit samples") {
  const auto source = planes(4, 4, 1, 12);
  const auto data = encode(OPJ_CODEC_J2K, 4, 4, 1, 12, source, OPJ_CLRSPC_GRAY);
  const JpxDecoder::Result result = decode(data);
  REQUIRE(result.fault.empty());
  CHECK(result.bits == 16U);
  const int value = (result.samples[2] * 256) + result.samples[3];
  CHECK(value == (source[0][1] * 65535 + 2047) / 4095);
}

TEST_CASE("data of no JPEG 2000 form is a fault; an area past the limit is refused") {
  const std::vector<std::uint8_t> junk{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  CHECK_FALSE(decode(junk).fault.empty());
  const auto data = encode(OPJ_CODEC_J2K, 64, 64, 3, 8, planes(64, 64, 3, 8), OPJ_CLRSPC_SRGB);
  std::vector<std::uint8_t> cut(data.begin(), data.begin() + 40);
  CHECK_FALSE(decode(cut).fault.empty());
  Budget::Limits limits;
  limits.stream_bytes = 4096;
  Budget budget(limits);
  try {
    (void)JpxDecoder::decode(std::as_bytes(std::span(data)), {}, budget, "object 3 0");
    FAIL("the decode was not refused");
  } catch (const pdfkit::platform::Error& error) {
    CHECK(error.code() == pdfkit::platform::ErrorCode::limit_exceeded);
  }
}

}  // namespace
