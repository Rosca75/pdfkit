// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ArithmeticDecoder is the MQ arithmetic decoder of JBIG2 (ITU-T T.88 annex E)
// and JPEG 2000 (ITU-T T.800 annex C): a probability-estimation state machine
// over a stream of coded bytes, one decision per call. It is a port of pdf.js
// src/core/arithmetic_decoder.js at commit fe48736846e2043696738bd54435ebb5ab24ecff
// (Apache-2.0), which follows the INITDEC, DECODE, BYTEIN and RENORMD
// procedures of the standard. A context is one byte holding the state index in
// its upper seven bits and the more probable symbol in its lowest bit, so a
// table of contexts is a vector of bytes that starts out zero.

#ifndef PDFKIT_IMAGES_ARITHMETIC_DECODER_HPP
#define PDFKIT_IMAGES_ARITHMETIC_DECODER_HPP

#include "platform/byte_span.hpp"

#include <cstdint>

namespace pdfkit::images {

class ArithmeticDecoder {
 public:
  // Starts decoding at the first byte of data. Reads past the end of data see
  // the 0xFF fill the standard prescribes for the end of a stream, so a stream
  // that ends early decodes to the symbols its fill implies and never reads
  // outside data.
  explicit ArithmeticDecoder(platform::ByteSpan data);

  // Decodes one decision under context, updating the context.
  [[nodiscard]] std::uint32_t read_bit(std::uint8_t& context);

 private:
  [[nodiscard]] std::uint32_t byte_at(std::size_t index) const noexcept;
  void byte_in();

  platform::ByteSpan data_;
  std::size_t position_ = 0;
  std::uint32_t chigh_ = 0;
  std::uint32_t clow_ = 0;
  std::uint32_t a_ = 0;
  std::int32_t count_ = 0;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_ARITHMETIC_DECODER_HPP
