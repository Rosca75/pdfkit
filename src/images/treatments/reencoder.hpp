// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Reencoder writes a treated image back, docs/DESIGN.md section 14. A
// lossless source (no codec, or the non-image filters alone) and a JPEG 2000
// source become Flate in the source's colour space at the samples' depth; a
// JBIG2 or CCITT source becomes 1-bit Flate (its codec's output is already
// the 1-bit samples a PDF image holds, so /Decode keeps its meaning); a DCT
// source becomes JPEG again through libjpeg-turbo at quality 90 (author
// decision) with the source's component count. The dictionary keeps every
// entry but the encoding's: /Filter and /DecodeParms (with /JBIG2Globals in
// them) are replaced, /Length and /DL go, /Width, /Height and
// /BitsPerComponent state the samples, a /ColorSpace the codec implied is
// written out, and /SMaskInData goes (the caller writes the alpha as a soft
// mask). /Decode is kept, because the samples keep their meaning, except on a
// JPEG 2000 image that is not a stencil, whose /Decode a reader ignores (ISO
// 32000-1 table 89, recalled) and which would start to apply under Flate.
// A Flate image is handed back as its packed samples, which the caller
// compresses with the writer's one Flate encoder (writer::StreamCopier), so
// every stream the writer creates is compressed alike.

#ifndef PDFKIT_IMAGES_TREATMENTS_REENCODER_HPP
#define PDFKIT_IMAGES_TREATMENTS_REENCODER_HPP

#include "images/treatments/pixel_buffer.hpp"
#include "syntax/dictionary.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace pdfkit::images::treatments {

class Reencoder {
 public:
  // The JPEG quality of a treated DCT image (author decision).
  static constexpr int jpeg_quality = 90;
  struct Result {
    // The image dictionary, full keys, without /Length; its /Filter is
    // /DCTDecode for a JPEG and absent for samples still to compress.
    syntax::Dictionary dictionary;
    // The JPEG bytes, or the packed samples (rows padded to a byte).
    std::vector<std::byte> data;
    // The data is a JPEG; otherwise the caller compresses it with Flate.
    bool jpeg = false;
  };

  // Encodes samples, the treated image whose dictionary (full keys) is
  // original and whose codec was codec ("" for none, else "DCTDecode",
  // "JPXDecode", "JBIG2Decode" or "CCITTFaxDecode"). image_mask marks a
  // stencil. Throws platform::Error (internal) when libjpeg-turbo fails.
  [[nodiscard]] static Result encode(const syntax::Dictionary& original, const PixelBuffer& samples,
                                     std::string_view codec, bool image_mask);

  // samples (8-bit, 1, 3 or 4 components) as a baseline JPEG at quality.
  [[nodiscard]] static std::vector<std::byte> jpeg(const PixelBuffer& samples, int quality);
};

}  // namespace pdfkit::images::treatments

#endif  // PDFKIT_IMAGES_TREATMENTS_REENCODER_HPP
