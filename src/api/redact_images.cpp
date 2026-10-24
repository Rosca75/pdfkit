// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 6, Redact images: pdfkit_redact_images adds image treatments (box, blur,
// remove) per image identity to an edit plan (CLAUDE.md section 2, docs/ABI.md). This build checks
// the arguments and then returns the not-implemented error the catalogue registry names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_redact_images(pdfkit_edit* edit,
                                                         const char* treatments_json,
                                                         pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_redact_images", "out");
    (void)pdfkit::api::require_edit(edit, "pdfkit_redact_images");
    (void)pdfkit::api::parse_json_object(treatments_json, "pdfkit_redact_images", "treatments_json",
                                         true);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_redact_images");
  });
}
