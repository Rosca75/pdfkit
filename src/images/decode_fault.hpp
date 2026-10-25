// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// DecodeFault is what a codec of the images layer throws when the data in
// front of it cannot be decoded any further: a damaged JPEG, a JBIG2 segment
// of an unknown type, a CCITT code that matches no table. It never leaves the
// layer: the decoder that catches it turns it into the undecodable_image
// warning of docs/DESIGN.md chapter 19, naming the fault, because an image
// that cannot be decoded is listed and kept, never a refusal of the document.
// A limit of the decoding budget is not a fault: it stays a platform::Error
// (limit_exceeded), which refuses the call as section 3.2 says.

#ifndef PDFKIT_IMAGES_DECODE_FAULT_HPP
#define PDFKIT_IMAGES_DECODE_FAULT_HPP

#include <stdexcept>
#include <string>

namespace pdfkit::images {

class DecodeFault : public std::runtime_error {
 public:
  // what names the fault in the warning's terms, such as "the JBIG2 segment 3
  // has the unknown type 99".
  explicit DecodeFault(const std::string& what) : std::runtime_error(what) {}
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_DECODE_FAULT_HPP
