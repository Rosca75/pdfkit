// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Conformance vectors for the JBIG2 decoder: the JBIG2 streams of the image of
// each pdf.js test file bitmap-<name>.pdf, from test/pdfs of
// https://github.com/mozilla/pdf.js at commit e2a5e1d34517b7e544f234d54eec3aa37df0b846
// (read; the repository's licence is Apache-2.0). Each file stores one
// 399 by 400 image coded with one feature of ITU-T T.88: a context template,
// custom adaptive pixels, typical prediction, MMR, refinement, symbol
// dictionaries and text regions in every reference corner (transposed or
// not), Huffman coding with standard and custom tables, context reuse, halftone
// regions with skip, striped pages and the file organisations. All but the
// composite-and-xnor file decode to one and the same bitmap, and the vectors
// were checked against PDFium's decoder (the bitmap's FNV-1a hash was taken
// from the decoded bytes that agreed with PDFium pixel for pixel).

#ifndef PDFKIT_TESTS_IMAGES_JBIG2_CONFORMANCE_VECTORS_HPP
#define PDFKIT_TESTS_IMAGES_JBIG2_CONFORMANCE_VECTORS_HPP

#include <cstdint>
#include <span>
#include <vector>

namespace pdfkit::test {

struct Jbig2Vector {
  const char* name;
  std::span<const std::uint8_t> bytes;
};

// FNV-1a over the packed rows the decoder returns for the 399 by 400 bitmap
// every vector stands for, and for the composite-and-xnor vector's own bitmap.
inline constexpr std::uint64_t k_jbig2_reference_hash = 0x444A755DB659F198ULL;
inline constexpr std::uint64_t k_jbig2_and_xnor_hash = 0x4830838E1867EC38ULL;

[[nodiscard]] const std::vector<Jbig2Vector>& jbig2_conformance_vectors();

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_IMAGES_JBIG2_CONFORMANCE_VECTORS_HPP
