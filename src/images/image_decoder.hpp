// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageDecoder turns an image into pixels, docs/DESIGN.md sections 9.3 and
// 9.4: the non-image filters of the syntax layer, then the codec the chain
// stopped at (DCT through libjpeg-turbo, JPX through OpenJPEG, JBIG2 and CCITT
// through the decoders written here), then the samples through /Decode and
// the colour space into grey or RGB, 8-bit (16 when the source is 16-bit, 1
// for a bitonal grey image). The base and the mask are decoded separately, on
// request, because combining a mask of another size is a resampling decision
// the caller makes. Every pixel buffer is checked against the decoding budget
// before it is reserved. An image that cannot be decoded is a result with a
// fault, which the caller reports as undecodable_image, never a refusal; a
// limit of the budget stays a limit_exceeded refusal of the call.

#ifndef PDFKIT_IMAGES_IMAGE_DECODER_HPP
#define PDFKIT_IMAGES_IMAGE_DECODER_HPP

#include "images/pixel_image.hpp"
#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace pdfkit::images {

class ImageDecoder {
 public:
  enum class Part : std::uint8_t { base, mask };

  struct Result {
    std::optional<PixelImage> image;
    // The fault when image is empty: what failed and in which encoding.
    std::string fault;
    // The image decoded with a recovery: a damaged filter, data shorter than
    // its size, libjpeg warnings, rows a codec read as white.
    bool damaged = false;
    // Part::mask only: the image has no mask (fault says so too).
    bool no_mask = false;
  };

  // An image XObject's base or mask.
  [[nodiscard]] static Result decode(syntax::Document& document, const syntax::Stream& stream,
                                     Part part);

  // An inline image's base or mask: its dictionary as written, its data, and
  // the /ColorSpace resource dictionary of the content that draws it (null for
  // none).
  [[nodiscard]] static Result decode_inline(syntax::Document& document,
                                            const syntax::Dictionary& dictionary,
                                            platform::ByteSpan data,
                                            const syntax::Object& colour_spaces, Part part);

  // The encoding of an image for listings: the last filter of its chain by its
  // full name ("DCTDecode", "FlateDecode"), or "none".
  [[nodiscard]] static std::string encoding(syntax::Document& document,
                                            const syntax::Dictionary& dictionary);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_IMAGE_DECODER_HPP
