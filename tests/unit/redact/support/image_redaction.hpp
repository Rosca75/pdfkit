// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The helpers the image treatment tests share: a three-page fixture written
// in code (one RGB image drawn on page 1, on page 2 and inside a form drawn
// on page 3, an inline image and a second image on page 1, a line of text,
// an image with a soft mask on page 2), an image treatment through the C ABI
// (open, edit, pdfkit_redact_images, an optional text match and page
// selection, pdfkit_write) that returns the statuses, the answers, the
// report and the output, and the reading of an output's images by the
// library's own listing and decoder, so a test asserts on what a reader of
// the written file finds.

#ifndef PDFKIT_TESTS_UNIT_REDACT_SUPPORT_IMAGE_REDACTION_HPP
#define PDFKIT_TESTS_UNIT_REDACT_SUPPORT_IMAGE_REDACTION_HPP

#include "pdfkit/pdfkit.h"

#include "images/image_decoder.hpp"
#include "platform/json_value.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pdfkit::test {

struct ImageFile {
  std::string bytes;
  // The images' encoded data as stored, by role: "shared" (16 by 12 RGB),
  // "kept" (8 by 8 grey), "inline" (4 by 4 grey), "masked" (16 by 16 RGB)
  // and "mask" (its soft mask).
  std::map<std::string, std::string> data;
  // The identities pdfkit_images reports, by the same roles.
  std::map<std::string, std::string> identity;
};

// The fixture described above.
[[nodiscard]] ImageFile image_file();

struct ImageRedaction {
  pdfkit_status treatment_status = PDFKIT_OK;
  std::string answer;
  pdfkit_status write_status = PDFKIT_OK;
  std::string report;
  std::string pdf;
};

// Treats file with treatments_json (the argument of pdfkit_redact_images);
// text_json, when not empty, is a pdfkit_redact_text argument given first;
// pages, when not empty, is a page selection.
[[nodiscard]] ImageRedaction treat(const std::string& file, const std::string& treatments_json,
                                   const std::string& text_json = "",
                                   const std::vector<std::uint32_t>& pages = {});

// pdfkit_images of bytes, parsed.
[[nodiscard]] platform::JsonValue list_images(const std::string& bytes);

// The samples of every image object of output whose /Width and /Height are
// width and height, by object number.
[[nodiscard]] std::map<std::uint32_t, images::ImageDecoder::Samples> samples_of(
    const std::string& output, std::uint32_t width, std::uint32_t height);

// The bytes of each content stream of output decoded (page contents and
// forms), joined.
[[nodiscard]] std::string contents_of(const std::string& output);

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_UNIT_REDACT_SUPPORT_IMAGE_REDACTION_HPP
