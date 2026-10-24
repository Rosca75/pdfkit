// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 3.1, List images: pdfkit_images lists every image XObject and inline image
// with its identity, encoding, size, colour space, masks and occurrences (CLAUDE.md section 2,
// docs/ABI.md). This build checks the arguments and then returns the not-implemented error the
// catalogue registry names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_images(pdfkit_document* doc, const char* options_json,
                                                  pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_images", "out");
    (void)pdfkit::api::require_document(doc, "pdfkit_images");
    (void)pdfkit::api::parse_json_object(options_json, "pdfkit_images", "options_json", false);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_images");
  });
}
