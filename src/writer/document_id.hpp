// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// DocumentId makes the trailer /ID of an output a function of its content,
// docs/DESIGN.md section 17: the file is written with a placeholder of zeros
// in both /ID elements, its SHA-256 is taken, and both placeholders are
// patched in place with the first 16 bytes of that hash, so two runs over one
// input agree and the original's identifier, which identifies the original,
// is gone.

#ifndef PDFKIT_WRITER_DOCUMENT_ID_HPP
#define PDFKIT_WRITER_DOCUMENT_ID_HPP

#include <cstddef>
#include <string>

namespace pdfkit::writer {

class DocumentId {
 public:
  // Hexadecimal digits of one element: 16 bytes.
  static constexpr std::size_t digits = 32;

  // The placeholder digits, all zero.
  [[nodiscard]] static std::string placeholder();

  // Hashes file (with zero placeholders at first and second) and writes the
  // identifier's digits over both; returns them.
  static std::string patch(std::string& file, std::size_t first, std::size_t second);
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_DOCUMENT_ID_HPP
