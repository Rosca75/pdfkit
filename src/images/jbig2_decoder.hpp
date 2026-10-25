// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Jbig2Decoder decodes JBIG2Decode data (ISO 32000-1 section 7.4.7, ITU-T
// T.88): the embedded stream organisation PDF uses, with the segments of
// /JBIG2Globals read first. It is a port of pdf.js src/core/jbig2.js
// (Apache-2.0); the implementation files name the commit read. The output is
// the filter's output as pdf.js produces it: the page bitmap with its JBIG2
// bits inverted, packed rows of (width + 7) / 8 bytes, a 0 bit black, so the
// image pipeline reads it as 1-bit DeviceGray samples like any other filter's
// output (a /Decode [1 0] flips it as usual).

#ifndef PDFKIT_IMAGES_JBIG2_DECODER_HPP
#define PDFKIT_IMAGES_JBIG2_DECODER_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::images {

class Jbig2Decoder {
 public:
  struct Result {
    // Packed rows of (width + 7) / 8 bytes each.
    std::vector<std::byte> data;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Empty when every segment decoded; otherwise the fault that stopped the
    // decoder (an unknown or unsupported segment type is named), and data is
    // empty.
    std::string fault;
  };

  // Decodes data with the segments of globals (empty when the image has no
  // /JBIG2Globals) read first. Every bitmap is checked against budget before
  // it is allocated; the output is charged as one stream named object.
  [[nodiscard]] static Result decode(platform::ByteSpan data, platform::ByteSpan globals,
                                     platform::Budget& budget, std::string_view object);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_JBIG2_DECODER_HPP
