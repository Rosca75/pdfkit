// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The images section of the dataset outcome, read from pdfkit_images over
// every page: "count" the images listed, "decoded" those whose "decodable" is
// true, "undecodable" the others with the warning code. A failed call is the
// section's status and code, as the other sections report theirs.

#include "verbs/dataset_images.hpp"

#include "json_text.hpp"
#include "output.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace pdfkit::cli::verbs {

std::string dataset_images(pdfkit_document* document) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_images(document, nullptr, &out);
  const std::string text(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  if (status == PDFKIT_E_NOT_IMPLEMENTED) {
    return R"({"status": "not_implemented", "code": "PDFKIT_E_NOT_IMPLEMENTED"})";
  }
  std::size_t offset = 0;
  const std::optional<JsonText> parsed = JsonText::parse(text, offset);
  if (status != PDFKIT_OK || !parsed) {
    const JsonText::Node* code = parsed ? parsed->member(parsed->root(), "code") : nullptr;
    const JsonText::Node* failed = parsed ? parsed->member(parsed->root(), "failed") : nullptr;
    return R"({"status": "error", "code": )" +
           json_quote(code != nullptr ? code->text : std::string("internal")) + R"(, "message": )" +
           json_quote(failed != nullptr ? failed->text : text) + "}";
  }
  const JsonText::Node* images = parsed->member(parsed->root(), "images");
  std::size_t count = 0;
  std::size_t decoded = 0;
  std::string undecodable;
  if (images != nullptr) {
    for (const std::size_t index : images->items) {
      const JsonText::Node& image = parsed->node(index);
      ++count;
      const JsonText::Node* decodable = parsed->member(image, "decodable");
      if (decodable != nullptr && decodable->raw == "true") {
        ++decoded;
        continue;
      }
      const JsonText::Node* identity = parsed->member(image, "identity");
      undecodable += undecodable.empty() ? "" : ", ";
      undecodable += R"({"identity": )" +
                     json_quote(identity != nullptr ? identity->text : std::string()) +
                     R"(, "code": "undecodable_image"})";
    }
  }
  return R"({"status": "ok", "code": null, "count": )" + std::to_string(count) +
         R"(, "decoded": )" + std::to_string(decoded) + R"(, "undecodable": [)" + undecodable +
         "]}";
}

}  // namespace pdfkit::cli::verbs
