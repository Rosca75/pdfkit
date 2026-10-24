// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The opaque handles of the C ABI and their checks. pdfkit_document and
// pdfkit_edit are named by the C header, so they live in the global namespace;
// the document holds its open options and the edit holds its document. The
// require functions turn a null handle into an invalid_argument error that
// names the symbol and how to obtain a valid handle.

#ifndef PDFKIT_API_HANDLES_HPP
#define PDFKIT_API_HANDLES_HPP

#include "pdfkit/pdfkit.h"

#include "platform/json_value.hpp"

#include <memory>
#include <string_view>

struct pdfkit_document {
  pdfkit::platform::JsonValue options;
};

struct pdfkit_edit {
  pdfkit_document* document;
};

namespace pdfkit::api {

[[nodiscard]] std::unique_ptr<pdfkit_document> new_document(platform::JsonValue options);
[[nodiscard]] std::unique_ptr<pdfkit_edit> new_edit(pdfkit_document& document);

// Each throws platform::Error (invalid_argument) naming symbol when the
// pointer is null.
pdfkit_document& require_document(pdfkit_document* doc, std::string_view symbol);
pdfkit_edit& require_edit(pdfkit_edit* edit, std::string_view symbol);

}  // namespace pdfkit::api

#endif  // PDFKIT_API_HANDLES_HPP
