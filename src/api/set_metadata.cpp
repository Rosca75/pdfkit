// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 7, Rewrite metadata: pdfkit_set_metadata sets the metadata field values an
// edit plan writes into both the Info dictionary and the XMP packet (CLAUDE.md section 2,
// docs/ABI.md). This build checks the arguments and then returns the not-implemented error the
// catalogue registry names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_set_metadata(pdfkit_edit* edit, const char* fields_json,
                                                        pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_set_metadata", "out");
    (void)pdfkit::api::require_edit(edit, "pdfkit_set_metadata");
    (void)pdfkit::api::parse_json_object(fields_json, "pdfkit_set_metadata", "fields_json", true);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_set_metadata");
  });
}
