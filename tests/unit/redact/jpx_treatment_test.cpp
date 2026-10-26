// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of a treated JPEG 2000 image through pdfkit_redact_images and
// pdfkit_write: an RGBA codestream under /SMaskInData 1 (encoded in the test
// by OpenJPEG's own encoder, losslessly, as the decoder's tests encode
// theirs) becomes a Flate image whose alpha is a soft mask of its own,
// opaque under the box; the codestream's bytes and /SMaskInData leave the
// output. The encoder helper repeats the one in
// tests/unit/images/jpx_decoder_test.cpp, which keeps its own.

#include "redact/support/image_redaction.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/document.hpp"
#include "syntax/limits.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>
#include <openjpeg.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

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

// A lossless J2K codestream of four 8-bit planes (red, green, blue, alpha).
std::string rgba_codestream(int width, int height) {
  std::vector<opj_image_cmptparm_t> parameters(4);
  for (opj_image_cmptparm_t& component : parameters) {
    std::memset(&component, 0, sizeof(component));
    component.dx = 1;
    component.dy = 1;
    component.w = static_cast<OPJ_UINT32>(width);
    component.h = static_cast<OPJ_UINT32>(height);
    component.prec = 8;
  }
  opj_image_t* image = opj_image_create(4, parameters.data(), OPJ_CLRSPC_SRGB);
  REQUIRE(image != nullptr);
  image->x1 = static_cast<OPJ_UINT32>(width);
  image->y1 = static_cast<OPJ_UINT32>(height);
  for (int c = 0; c < 4; ++c) {
    for (int i = 0; i < width * height; ++i) {
      image->comps[c].data[i] = (i * 37 + c * 91) % 200 + 20;
    }
  }
  opj_cparameters_t settings;
  opj_set_default_encoder_parameters(&settings);
  settings.tcp_numlayers = 1;
  settings.tcp_rates[0] = 0;
  settings.cp_disto_alloc = 1;
  settings.numresolution = 2;
  opj_codec_t* codec = opj_create_compress(OPJ_CODEC_J2K);
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
  return {reinterpret_cast<const char*>(sink.bytes.data()), sink.bytes.size()};
}

}  // namespace

TEST_CASE("a JPEG 2000 image's in-data alpha becomes a soft mask, opaque under the box") {
  const std::string codestream = rgba_codestream(16, 16);
  pdfkit::test::PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t image = builder.add_stream(
      "/Type /XObject /Subtype /Image /Width 16 /Height 16 /BitsPerComponent 8 "
      "/SMaskInData 1 /Filter /JPXDecode",
      codestream);
  builder.add_page("q 160 0 0 160 100 100 cm /Im0 Do Q",
                   "/Resources << /XObject << /Im0 " + std::to_string(image) + " 0 R >> >>");
  const std::string file = builder.build();
  const auto listed = pdfkit::test::list_images(file);
  const std::string identity =
      listed.find("images")->as_array().at(0).find("identity")->as_string();
  const pdfkit::test::ImageRedaction result =
      pdfkit::test::treat(file, R"({"treatments": [{"identity": ")" + identity +
                                    R"(", "treatment": "box", "rectangle": [0, 0, 8, 8]}]})");
  INFO(result.answer);
  INFO(result.report);
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(result.pdf.find("/JPXDecode") == std::string::npos);
  CHECK(result.pdf.find("/SMaskInData") == std::string::npos);
  CHECK_FALSE(pdfkit::test::contains_marker(result.pdf, codestream));
  const auto found = pdfkit::test::samples_of(result.pdf, 16, 16);
  REQUIRE(found.size() == 2U);
  for (const auto& [number, samples] : found) {
    const bool mask = samples.buffer.components() == 1;
    CHECK(samples.codec.empty());
    for (std::uint32_t y = 0; y < 16; ++y) {
      for (std::uint32_t x = 0; x < 16; ++x) {
        if (x < 8 && y < 8) {
          REQUIRE(samples.buffer.at(x, y, 0) == (mask ? 255 : 0));
        } else if (mask) {
          REQUIRE(
              samples.buffer.at(x, y, 0) ==
              static_cast<std::uint16_t>((static_cast<int>(y * 16 + x) * 37 + 3 * 91) % 200 + 20));
        }
      }
    }
  }
}
