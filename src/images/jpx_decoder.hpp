// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// JpxDecoder decodes JPXDecode data through OpenJPEG, docs/DESIGN.md section
// 9.3: a bare J2K codestream or a JP2 file, told apart by their signatures,
// at full resolution. The components become interleaved samples of 8 bits,
// or 16 when the codestream's precision is above 8; a component sampled more
// coarsely than the image (XRsiz or YRsiz above 1) is upsampled by
// replication, each sample repeated over its dx by dy cell. An sYCC
// codestream is converted to RGB. The decoded area is checked against the
// decoding budget before OpenJPEG decodes the tiles (OpenJPEG holds a 32-bit
// integer per sample). OpenJPEG's messages are kept, never printed: the last
// error becomes the fault. The calls follow the OpenJPEG API documentation
// and its opj_decompress tool (recalled).

#ifndef PDFKIT_IMAGES_JPX_DECODER_HPP
#define PDFKIT_IMAGES_JPX_DECODER_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::images {

class JpxDecoder {
 public:
  struct Options {
    // The component count the PDF's /ColorSpace expects, or 0 to take the
    // codestream's own (1 grey, 3 RGB, 4 CMYK).
    std::uint32_t components = 0;
    // Keep the samples unscaled (an Indexed image's samples are indices).
    bool raw = false;
    // /SMaskInData 1 or 2: the component after the colour components is the
    // image's soft mask.
    bool alpha = false;
  };

  struct Result {
    // width * height * components samples of bits / 8 bytes, 16-bit most
    // significant byte first.
    std::vector<std::uint8_t> samples;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t components = 0;
    // 8 or 16.
    std::uint32_t bits = 8;
    // The soft mask, 8-bit, width * height bytes, when Options::alpha found one.
    std::vector<std::uint8_t> alpha;
    // Empty when decoded; otherwise what OpenJPEG or the mapping reported.
    std::string fault;
  };

  [[nodiscard]] static Result decode(platform::ByteSpan data, const Options& options,
                                     platform::Budget& budget, std::string_view object);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_JPX_DECODER_HPP
