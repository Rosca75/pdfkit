// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 9, Select pages: pdfkit_select_pages sets the pages an edit plan keeps, in the
// order given (CLAUDE.md section 2, docs/ABI.md). This build checks the arguments and then returns
// the not-implemented error the catalogue registry names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_select_pages(pdfkit_edit* edit, const char* pages_json,
                                                        pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_select_pages", "out");
    (void)pdfkit::api::require_edit(edit, "pdfkit_select_pages");
    (void)pdfkit::api::parse_json_object(pages_json, "pdfkit_select_pages", "pages_json", true);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_select_pages");
  });
}
