// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The CCITT decoder's entry. This build decodes nothing: every call answers
// the fault that names the missing decoder, so the image pipeline lists the
// image as undecodable until the port lands.

#include "images/ccitt_decoder.hpp"

namespace pdfkit::images {

CcittDecoder::Result CcittDecoder::decode(platform::ByteSpan /*data*/,
                                          const Parameters& /*parameters*/,
                                          platform::Budget& /*budget*/,
                                          std::string_view /*object*/) {
  Result result;
  result.fault = "this build of pdfkit has no CCITT decoder";
  return result;
}

}  // namespace pdfkit::images
