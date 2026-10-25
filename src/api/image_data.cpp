// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 3.2, Image pixels: pdfkit_image_data returns one image's
// bytes by its identity (CLAUDE.md section 2, docs/ABI.md section 4.8): a PNG
// of a lossless source, the original JPEG bytes of a DCT source, or the mask
// as its own PNG. The options are {"mask": bool, "decode_jpeg": bool}. An
// identity that names no image of the document is invalid_argument; an image
// whose encoding does not decode is refused with undecodable_image, naming
// the identity, the encoding and the fault.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "images/image_catalogue.hpp"
#include "images/image_decoder.hpp"
#include "images/image_extractor.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"

#include <memory>
#include <string>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;

bool flag(const JsonValue& options, const std::string& key) {
  const JsonValue* value = options.find(key);
  if (value == nullptr) {
    return false;
  }
  if (value->kind() != JsonValue::Kind::boolean) {
    throw Error(ErrorCode::invalid_argument,
                "pdfkit_image_data options_json: \"" + key + "\" is " +
                    std::string(JsonValue::kind_name(value->kind())),
                "true or false", "give \"" + key + "\" as a JSON boolean");
  }
  return value->as_bool();
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_image_data(pdfkit_document* doc, const char* identity,
                                                      const char* options_json,
                                                      pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_image_data", "out");
    pdfkit_document& handle = pdfkit::api::require_document(doc, "pdfkit_image_data");
    const std::string wanted(
        pdfkit::api::require_string(identity, "pdfkit_image_data", "identity"));
    const JsonValue options =
        pdfkit::api::parse_json_object(options_json, "pdfkit_image_data", "options_json", false);
    for (const auto& [key, value] : options.as_object()) {
      if (key != "mask" && key != "decode_jpeg") {
        throw Error(ErrorCode::invalid_argument,
                    "pdfkit_image_data options_json names the unknown option \"" + key + "\"",
                    "the options \"mask\" and \"decode_jpeg\", each true or false",
                    "remove \"" + key + "\" from options_json");
      }
    }
    pdfkit::images::ImageExtractor::Request request;
    request.mask = flag(options, "mask");
    request.decode_jpeg = flag(options, "decode_jpeg");
    pdfkit::syntax::Document& document = *handle.document;
    if (!handle.images) {
      handle.images = std::make_unique<pdfkit::images::ImageCatalogue>(document);
    }
    const pdfkit::images::ImageCatalogue::Entry* entry = handle.images->find(wanted);
    if (entry == nullptr) {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_image_data identity \"" + wanted + "\" names no image of the document",
                  "an identity pdfkit_images listed for this document, such as "
                  "\"img:0123456789abcdef\"",
                  "list the document's images with pdfkit_images and pass one of their "
                  "identities");
    }
    const pdfkit::syntax::Stream& stream = *entry->stream.stream();
    const pdfkit::images::ImageExtractor::Output output =
        pdfkit::images::ImageExtractor::extract(document, stream, request);
    if (output.no_mask) {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_image_data asked for the mask of " + wanted + ", which has none",
                  "an image with /SMask, /Mask or /SMaskInData",
                  "ask for the base image (\"mask\": false)");
    }
    if (!output.fault.empty()) {
      const std::string encoding =
          pdfkit::images::ImageDecoder::encoding(document, stream.dictionary());
      throw Error(ErrorCode::undecodable_image,
                  "the image " + wanted + " (" + encoding + ", object " +
                      std::to_string(entry->object_number) + ") does not decode: " + output.fault,
                  "an image whose " + encoding + " data decodes",
                  "the image can be kept or removed but not read; re-save the file with its "
                  "producing application if another reader shows the image");
    }
    pdfkit::api::write_buffer(out, std::string(output.bytes.begin(), output.bytes.end()));
    return PDFKIT_OK;
  });
}
