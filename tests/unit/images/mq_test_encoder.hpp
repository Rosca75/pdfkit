// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// MqTestEncoder is the MQ arithmetic encoder of JBIG2 (ITU-T T.88 annex E.2,
// the same coder as ITU-T T.800 annex C.2, recalled) with the arithmetic integer
// encoding of T.88 annex A. It is test code: an encoder written from the
// standard's flowcharts is the cheapest independent proof of the decoder, since
// a round trip fails whenever the two disagree about a probability state, a
// renormalisation or a byte-stuffing rule.

#ifndef PDFKIT_TESTS_IMAGES_MQ_TEST_ENCODER_HPP
#define PDFKIT_TESTS_IMAGES_MQ_TEST_ENCODER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pdfkit::test {

class MqTestEncoder {
 public:
  // The integer contexts of one annex A procedure.
  using IntegerContexts = std::array<std::uint8_t, 512>;

  MqTestEncoder();

  // Encodes one decision under context (state index in the upper seven bits,
  // more probable symbol in bit 0), updating the context.
  void encode(std::uint8_t& context, unsigned bit);

  // Annex A.2: a value, or the out-of-band value when none is given.
  void encode_integer(IntegerContexts& contexts, std::optional<std::int64_t> value);

  // Annex A.3: a symbol ID of code_length bits.
  void encode_iaid(std::vector<std::uint8_t>& contexts, unsigned code_length, std::uint32_t value);

  // Flushes the coder and ends the data with the 0xFF 0xAC marker.
  [[nodiscard]] std::vector<std::byte> finish();

 private:
  void renormalise();
  void byte_out();

  std::uint32_t a_ = 0x8000;
  std::uint32_t c_ = 0;
  int count_ = 12;
  // bytes_[0] stands for the byte before the first one of the data.
  std::vector<std::uint8_t> bytes_ = {0};
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_IMAGES_MQ_TEST_ENCODER_HPP
