// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The JBIG2 decoder's entry. This build decodes nothing: every call answers
// the fault that names the missing decoder, so the image pipeline lists the
// image as undecodable until the port lands.

#include "images/jbig2_decoder.hpp"

namespace pdfkit::images {

Jbig2Decoder::Result Jbig2Decoder::decode(platform::ByteSpan /*data*/,
                                          platform::ByteSpan /*globals*/,
                                          platform::Budget& /*budget*/,
                                          std::string_view /*object*/) {
  Result result;
  result.fault = "this build of pdfkit has no JBIG2 decoder";
  return result;
}

}  // namespace pdfkit::images
