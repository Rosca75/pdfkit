// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 2, Extract text: pdfkit_text returns each page's blocks, lines and words in
// reading order and the glyph records behind them (CLAUDE.md section 2, docs/ABI.md). This build
// checks the arguments and then returns the not-implemented error the catalogue registry names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/export.hpp"

extern "C" PDFKIT_API pdfkit_status pdfkit_text(pdfkit_document* doc, const char* options_json,
                                                pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_text", "out");
    (void)pdfkit::api::require_document(doc, "pdfkit_text");
    (void)pdfkit::api::parse_json_object(options_json, "pdfkit_text", "options_json", false);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_text");
  });
}
