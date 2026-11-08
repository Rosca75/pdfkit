// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Filters are the non-image stream filters of ISO 32000-1 section 7.4 (read),
// docs/DESIGN.md section 3.1: FlateDecode through zlib with a raw-or-zlib
// header tolerance, LZWDecode with /EarlyChange, ASCII85Decode,
// ASCIIHexDecode, RunLengthDecode, the TIFF and PNG predictors, and Crypt as
// the identity (the security handler decrypts before). A chain is applied in
// the order /Filter lists it. A chain stops at the first image codec
// (DCTDecode, CCITTFaxDecode, JBIG2Decode, JPXDecode) or unknown filter and
// hands back the bytes so far with the filters left, because the images layer
// decodes those and the writer copies them untouched. A filter that meets a
// fault yields what it decoded before it and marks the result damaged with
// the fault's damage class, as every viewer reads such a stream. Every byte
// produced is charged to the document's budget.

#ifndef PDFKIT_SYNTAX_FILTERS_HPP
#define PDFKIT_SYNTAX_FILTERS_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "syntax/object.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::syntax {

class Filters {
 public:
  struct Result {
    std::vector<std::byte> bytes;
    // The filters not applied (an image codec and what follows it, or an
    // unknown filter), with their /DecodeParms entries; empty when the chain
    // was decoded to the end.
    std::vector<std::string> remaining;
    std::vector<Object> remaining_parameters;
    // A filter stopped at a fault; bytes holds what decoded before it.
    bool damaged = false;
    std::string fault;
    // The fault's damage class (docs/DESIGN.md section 3.1): flate_checksum,
    // flate_corrupt, flate_truncated, flate_dictionary, lzw, ascii_hex,
    // ascii85, run_length or predictor; empty when nothing is damaged.
    std::string damage;
    // The bytes of the faulting filter's input it did not read, when the
    // filter knows where it stopped.
    std::optional<std::size_t> unread;
    // The stream's data is an external file (/F), which is never read, so
    // bytes is empty.
    bool external = false;
  };

  // Applies the chain filter (a name, an array of names or null) with its
  // parameters (a dictionary, an array of dictionaries and nulls, or null),
  // both already resolved. object names the stream in a refusal.
  [[nodiscard]] static Result decode(platform::ByteSpan data, const Object& filter,
                                     const Object& parameters, platform::Budget& budget,
                                     std::string_view object);

  // The full name of an inline image's abbreviated filter name, or the name
  // itself (ISO 32000-1 section 8.9.7, table 94).
  [[nodiscard]] static std::string_view full_name(std::string_view name) noexcept;

  // Whether a filter is one of the image codecs the chain stops at.
  [[nodiscard]] static bool is_image_codec(std::string_view name) noexcept;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_FILTERS_HPP
