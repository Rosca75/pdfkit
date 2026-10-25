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

#include "images/colour_space.hpp"
#include "images/pixel_image.hpp"
#include "images/treatments/pixel_buffer.hpp"
#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

  // An image's samples as its encoding stores them, before /Decode and the
  // colour space, which a treatment paints and re-encodes
  // (docs/DESIGN.md section 14).
  struct Samples {
    treatments::PixelBuffer buffer;
    // The colour space; null for a stencil.
    std::shared_ptr<const ColourSpace> space;
    // Two numbers per component: /Decode, the default filled in (and the
    // default for a JPEG 2000 image that is not a stencil, which reads none).
    std::vector<double> decode;
    // The codec the filter chain ends with ("DCTDecode", "JPXDecode",
    // "JBIG2Decode", "CCITTFaxDecode"), or empty.
    std::string codec;
    bool image_mask = false;
    // A JPEG 2000 codestream's alpha under /SMaskInData, 8-bit, or empty.
    std::vector<std::uint8_t> alpha;
    // A colour-key /Mask as raw value ranges, two per component, or empty.
    std::vector<std::uint32_t> colour_key;
    // Read with a recovery.
    bool damaged = false;
  };

  struct SamplesResult {
    std::optional<Samples> samples;
    // Why the samples could not be read, when samples is empty.
    std::string fault;
  };

  // An image XObject's samples, or a soft mask's (soft_mask: read as grey
  // whatever its dictionary states). A budget limit throws, as decode does.
  [[nodiscard]] static SamplesResult samples(syntax::Document& document,
                                             const syntax::Stream& stream, bool soft_mask);

  // An inline image's samples: its dictionary as written, its data, and the
  // /ColorSpace resources of the content that draws it.
  [[nodiscard]] static SamplesResult samples_inline(syntax::Document& document,
                                                    const syntax::Dictionary& dictionary,
                                                    platform::ByteSpan data,
                                                    const syntax::Object& colour_spaces);

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
