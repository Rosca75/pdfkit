// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// CcittTestEncoder writes bitonal rows as CCITT Group 3 (one- and
// two-dimensional) and Group 4 data, following ITU-T T.4 and T.6 (the code
// tables were recalled and cross-checked against the decoder's tables). It is
// test code: an encoder written from the standard is the cheapest independent
// proof of a decoder, because a round trip fails whenever the two disagree about
// a code. The JBIG2 tests reuse it for MMR-coded regions.

#ifndef PDFKIT_TESTS_IMAGES_CCITT_TEST_ENCODER_HPP
#define PDFKIT_TESTS_IMAGES_CCITT_TEST_ENCODER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::test {

// Rows of pixels, true for black.
using BitonalRows = std::vector<std::vector<bool>>;

class CcittTestEncoder {
 public:
  struct Options {
    // 0: Group 3 one-dimensional; > 0: Group 3 two-dimensional with one
    // one-dimensional row in every k; < 0: Group 4.
    int k = -1;
    // Group 3 rows start with an end-of-line code (always on for k > 0).
    bool end_of_line = false;
    // Every row starts on a byte boundary.
    bool byte_align = false;
    // Group 4 ends with the end-of-facsimile-block code, Group 3 with six
    // end-of-line codes (each followed by a 1 tag bit in the two-dimensional
    // coding).
    bool end_of_block = true;
  };

  // Encodes rows, all of one width.
  [[nodiscard]] static std::vector<std::byte> encode(const BitonalRows& rows,
                                                     const Options& options);

  // The code of one run as a string of '0' and '1' (make-up codes, then the
  // terminating code), for tests that assemble a damaged row by hand.
  [[nodiscard]] static std::string run_code(bool black, std::size_t run);

  // Packs a string of '0' and '1' into bytes, padding the last byte with zeros.
  [[nodiscard]] static std::vector<std::byte> pack(std::string_view bits);
};

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_IMAGES_CCITT_TEST_ENCODER_HPP
