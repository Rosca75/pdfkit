// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// DctDecoder decodes DCTDecode data through libjpeg-turbo, docs/DESIGN.md
// section 9.3: grey, RGB (and YCbCr), CMYK and YCCK JPEGs, 8- and 12-bit, to
// interleaved 8-bit samples as stored (CMYK stays CMYK). The libjpeg calls run
// inside the C guard of dct_guard.c, because libjpeg reports an error by a
// longjmp, and no longjmp may cross a frame holding a C++ object: the rule is
// that the C frame owns only the decompressor and destroys it on both paths.
// The pixel buffer is checked against the decoding budget before
// jpeg_start_decompress and charged when decoded. Whether the stream holds an
// Adobe APP14 marker is reported, because a CMYK JPEG with one stores
// inverted values (pdf.js and PDFium behaviour, recalled) and the colour
// pipeline inverts them.

#ifndef PDFKIT_IMAGES_DCT_DECODER_HPP
#define PDFKIT_IMAGES_DCT_DECODER_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::images {

class DctDecoder {
 public:
  struct Result {
    // width * height * components bytes.
    std::vector<std::uint8_t> samples;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t components = 0;
    bool adobe = false;
    // libjpeg's warnings (corrupt data it read past, a premature end).
    std::uint32_t warnings = 0;
    // Empty when decoded; otherwise libjpeg's message.
    std::string fault;
  };

  // color_transform is /ColorTransform of /DecodeParms (0 or 1), or -1 when
  // absent, which leaves the choice to the JPEG's own markers.
  [[nodiscard]] static Result decode(platform::ByteSpan data, int color_transform,
                                     platform::Budget& budget, std::string_view object);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_DCT_DECODER_HPP
