// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 10.1, Write: pdfkit_write serialises an edit plan into a fresh PDF,
// self-checks it and returns its bytes and the write report (CLAUDE.md section 2, docs/ABI.md).
// This build checks the arguments and then returns the not-implemented error the catalogue registry
// names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_write(pdfkit_edit* edit, const char* options_json,
                                                 pdfkit_buffer* out_pdf, pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_write", "out");
    (void)pdfkit::api::require_edit(edit, "pdfkit_write");
    (void)pdfkit::api::parse_json_object(options_json, "pdfkit_write", "options_json", false);
    pdfkit::api::require_out(out_pdf, "pdfkit_write", "out_pdf");
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_write");
  });
}
