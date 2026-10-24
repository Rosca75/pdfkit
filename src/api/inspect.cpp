// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 1.2, Inspect: pdfkit_inspect reports a document's version, page count, page
// boxes and rotation, encryption, repairs and warnings (CLAUDE.md section 2, docs/ABI.md). This
// build checks the arguments and then returns the not-implemented error the catalogue registry
// names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_inspect(pdfkit_document* doc, const char* options_json,
                                                   pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_inspect", "out");
    (void)pdfkit::api::require_document(doc, "pdfkit_inspect");
    (void)pdfkit::api::parse_json_object(options_json, "pdfkit_inspect", "options_json", false);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_inspect");
  });
}
