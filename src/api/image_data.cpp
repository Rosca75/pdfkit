// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 3.2, Image pixels: pdfkit_image_data returns one image's pixels as PNG, or its
// original JPEG bytes for a DCT source (CLAUDE.md section 2, docs/ABI.md). This build checks the
// arguments and then returns the not-implemented error the catalogue registry names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_image_data(pdfkit_document* doc, const char* identity,
                                                      const char* options_json,
                                                      pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_image_data", "out");
    (void)pdfkit::api::require_document(doc, "pdfkit_image_data");
    (void)pdfkit::api::require_string(identity, "pdfkit_image_data", "identity");
    (void)pdfkit::api::parse_json_object(options_json, "pdfkit_image_data", "options_json", false);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_image_data");
  });
}
