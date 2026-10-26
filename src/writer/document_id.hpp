// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// DocumentId makes the trailer /ID of an output a function of its content,
// docs/DESIGN.md section 17: the file is written with a placeholder of zeros
// in both /ID elements, its SHA-256 is taken, and both placeholders are
// patched in place with the first 16 bytes of that hash, so two runs over one
// input agree and the original's identifier, which identifies the original,
// is gone. The XMP packet's identifiers are made the same way: the metadata
// writer writes xmpMM:DocumentID and xmpMM:InstanceID as placeholders of the
// same length, the hash is taken over the file holding every placeholder, and
// the packet's are patched with "xmp.did:" and "xmp.iid:" followed by the
// first and the second 16 bytes of the hash in hexadecimal, so the packet
// names the output and not the original.

#ifndef PDFKIT_WRITER_DOCUMENT_ID_HPP
#define PDFKIT_WRITER_DOCUMENT_ID_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace pdfkit::writer {

class DocumentId {
 public:
  // Hexadecimal digits of one element: 16 bytes.
  static constexpr std::size_t digits = 32;

  // The placeholder digits, all zero.
  [[nodiscard]] static std::string placeholder();

  // The XMP identifiers' placeholders.
  static constexpr std::string_view xmp_document_placeholder =
      "xmp.did:00000000000000000000000000000000";
  static constexpr std::string_view xmp_instance_placeholder =
      "xmp.iid:00000000000000000000000000000000";

  // Hashes file (with zero placeholders at first and second, and the XMP
  // placeholders inside [packet_begin, packet_end)) and writes the
  // identifier's digits over both /ID elements and the XMP placeholders;
  // returns the /ID digits.
  static std::string patch(std::string& file, std::size_t first, std::size_t second,
                           std::size_t packet_begin = 0, std::size_t packet_end = 0);
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_DOCUMENT_ID_HPP
