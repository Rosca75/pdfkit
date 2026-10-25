// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// CcittDecoder decodes CCITTFaxDecode data (ISO 32000-1 section 7.4.6, the
// ITU-T T.4 and T.6 codings): Group 3 one-dimensional (/K 0), Group 3
// two-dimensional (/K > 0) and Group 4 (/K < 0), with /EncodedByteAlign,
// /EndOfLine, /EndOfBlock, /BlackIs1, /Columns, /Rows and
// /DamagedRowsBeforeError. It is a port of pdf.js src/core/ccitt.js
// (Apache-2.0); the implementation file names the commit read. The output is
// the filter's output as the specification defines it: packed rows of
// (columns + 7) / 8 bytes, a 0 bit black unless /BlackIs1 is true, so the
// image pipeline reads it as 1-bit samples like any other filter's output.
// The JBIG2 decoder reuses decode for its MMR-coded regions.

#ifndef PDFKIT_IMAGES_CCITT_DECODER_HPP
#define PDFKIT_IMAGES_CCITT_DECODER_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::images {

class CcittDecoder {
 public:
  // The /DecodeParms entries of table 11, with their defaults.
  struct Parameters {
    std::int64_t k = 0;
    bool end_of_line = false;
    bool encoded_byte_align = false;
    std::int64_t columns = 1728;
    // 0: as many rows as the data holds.
    std::int64_t rows = 0;
    bool end_of_block = true;
    bool black_is_1 = false;
    std::int64_t damaged_rows_before_error = 0;
  };

  struct Result {
    // Packed rows of (columns + 7) / 8 bytes each.
    std::vector<std::byte> data;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    // Rows whose codes did not decode, read as white.
    std::uint32_t damaged_rows = 0;
    // The bytes of data the decoder read, counting the end-of-block code of a
    // Group 4 stream that stops at /Rows; the next stream of a run of them starts
    // here.
    std::size_t bytes_consumed = 0;
    // Empty when the data decoded to its end (or to /Rows); otherwise the
    // fault that stopped the decoder, with data holding the rows before it.
    std::string fault;
  };

  // Decodes data under parameters. The output is charged to budget as one
  // stream named object; a /Columns past the per-stream limit is refused with
  // limit_exceeded before anything is reserved.
  [[nodiscard]] static Result decode(platform::ByteSpan data, const Parameters& parameters,
                                     platform::Budget& budget, std::string_view object);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_CCITT_DECODER_HPP
