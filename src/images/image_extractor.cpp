// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The extraction of one image as file bytes. A DCT source's bytes are handed
// back only when the chain ends at DCTDecode; a JPEG behind another filter
// after it cannot be stored as a JPEG file and is decoded instead.

#include "images/image_extractor.hpp"

#include "images/image_decoder.hpp"
#include "images/png_writer.hpp"
#include "syntax/filters.hpp"

#include <algorithm>

namespace pdfkit::images {

ImageExtractor::Output ImageExtractor::extract(syntax::Document& document,
                                               const syntax::Stream& stream,
                                               const Request& request) {
  Output output;
  if (!request.mask && !request.decode_jpeg) {
    syntax::Filters::Result decoded = document.decode(stream);
    if (decoded.remaining.size() == 1 &&
        syntax::Filters::full_name(decoded.remaining.front()) == "DCTDecode" &&
        !decoded.bytes.empty()) {
      output.bytes.resize(decoded.bytes.size());
      std::ranges::transform(decoded.bytes, output.bytes.begin(),
                             [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
      output.format = "jpeg";
      return output;
    }
  }
  const ImageDecoder::Result result = ImageDecoder::decode(
      document, stream, request.mask ? ImageDecoder::Part::mask : ImageDecoder::Part::base);
  if (!result.image) {
    output.fault = result.fault;
    output.no_mask = result.no_mask;
    return output;
  }
  output.bytes = PngWriter::write(*result.image);
  output.format = "png";
  return output;
}

ImageExtractor::Output ImageExtractor::extract_inline(syntax::Document& document,
                                                      const syntax::Dictionary& dictionary,
                                                      platform::ByteSpan data,
                                                      const syntax::Object& colour_spaces,
                                                      const Request& request) {
  Output output;
  const ImageDecoder::Result result = ImageDecoder::decode_inline(
      document, dictionary, data, colour_spaces,
      request.mask ? ImageDecoder::Part::mask : ImageDecoder::Part::base);
  if (!request.mask && !request.decode_jpeg &&
      ImageDecoder::encoding(document, dictionary) == "DCTDecode" && result.image) {
    // The JPEG an inline image holds is its data after any filter before the
    // codec, which the decoder has read; with no such filter it is the data.
    const syntax::Object filter =
        dictionary.contains("F") ? dictionary.get("F") : dictionary.get("Filter");
    if (filter.name() != nullptr) {
      output.bytes.resize(data.size());
      std::ranges::transform(data, output.bytes.begin(),
                             [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
      output.format = "jpeg";
      return output;
    }
  }
  if (!result.image) {
    output.fault = result.fault;
    output.no_mask = result.no_mask;
    return output;
  }
  output.bytes = PngWriter::write(*result.image);
  output.format = "png";
  return output;
}

}  // namespace pdfkit::images
