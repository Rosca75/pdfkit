// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 1.1, Open: pdfkit_open reads PDF bytes into a document handle, repairing what
// it can and decrypting with an empty user password, and returns the open report (CLAUDE.md section
// 2, docs/ABI.md). This build checks the arguments and then returns the not-implemented error the
// catalogue registry names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"

#include <cstddef>
#include <cstdint>

extern "C" PDFKIT_API pdfkit_status pdfkit_open(const std::uint8_t* bytes, std::size_t size,
                                                const char* options_json, pdfkit_document** out_doc,
                                                pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_open", "out");
    pdfkit::api::require_bytes(bytes, size, "pdfkit_open");
    (void)pdfkit::api::parse_json_object(options_json, "pdfkit_open", "options_json", false);
    if (out_doc == nullptr) {
      throw pdfkit::platform::Error(pdfkit::platform::ErrorCode::invalid_argument,
                                    "pdfkit_open was called with a null out_doc",
                                    "a pointer to a pdfkit_document* that receives the handle",
                                    "pass the address of a pdfkit_document* variable");
    }
    *out_doc = nullptr;
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_open");
  });
}
