// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 8, Strip hidden channels: pdfkit_channels reports, per row of the channel
// table, whether the channel is present and its count (CLAUDE.md section 2, docs/ABI.md). This
// build checks the arguments and then returns the not-implemented error the catalogue registry
// names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_channels(pdfkit_document* doc, const char* options_json,
                                                    pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_channels", "out");
    (void)pdfkit::api::require_document(doc, "pdfkit_channels");
    (void)pdfkit::api::parse_json_object(options_json, "pdfkit_channels", "options_json", false);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_channels");
  });
}
