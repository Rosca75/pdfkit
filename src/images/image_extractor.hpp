// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageExtractor answers pdfkit_image_data, docs/DESIGN.md section 9.5: for a
// DCTDecode source, its JPEG bytes unchanged (the JFIF or Adobe stream as
// stored, after any non-image filter before the codec), because re-encoding a
// JPEG loses quality and adds nothing; for every other source, and for a JPEG
// when the caller asks for its pixels, a PNG of the decoded image; the mask
// (soft, stencil, colour key or in-data alpha) as its own PNG when asked for.

#ifndef PDFKIT_IMAGES_IMAGE_EXTRACTOR_HPP
#define PDFKIT_IMAGES_IMAGE_EXTRACTOR_HPP

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pdfkit::images {

class ImageExtractor {
 public:
  struct Request {
    // The mask instead of the base image.
    bool mask = false;
    // A PNG of a DCT source's decoded pixels instead of its JPEG bytes.
    bool decode_jpeg = false;
  };

  struct Output {
    std::vector<std::uint8_t> bytes;
    // "png" or "jpeg".
    std::string format;
    // Empty on success; otherwise why nothing was extracted.
    std::string fault;
    // The image has no mask (with Request::mask).
    bool no_mask = false;
  };

  [[nodiscard]] static Output extract(syntax::Document& document, const syntax::Stream& stream,
                                      const Request& request);

  // The same for an inline image: its dictionary as written, its data, and
  // the /ColorSpace resource dictionary of the content that drew it.
  [[nodiscard]] static Output extract_inline(syntax::Document& document,
                                             const syntax::Dictionary& dictionary,
                                             platform::ByteSpan data,
                                             const syntax::Object& colour_spaces,
                                             const Request& request);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_IMAGE_EXTRACTOR_HPP
