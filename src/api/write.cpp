// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 10.1, Write: pdfkit_write applies an edit plan through
// the write pipeline and returns a fresh PDF of the reachable objects,
// stripped of every channel of the channel table, with the write report
// (CLAUDE.md section 2, docs/ABI.md). It takes no options; an unknown one is
// refused, because a misspelt option that silently does nothing is an option
// the caller believes applied. The PDF reaches out_pdf only when the whole
// write succeeded.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "redact/write_pipeline.hpp"

#include <utility>

extern "C" PDFKIT_API pdfkit_status pdfkit_write(pdfkit_edit* edit, const char* options_json,
                                                 pdfkit_buffer* out_pdf, pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_write", "out");
    pdfkit_edit& handle = pdfkit::api::require_edit(edit, "pdfkit_write");
    const pdfkit::platform::JsonValue options =
        pdfkit::api::parse_json_object(options_json, "pdfkit_write", "options_json", false);
    pdfkit::api::require_out(out_pdf, "pdfkit_write", "out_pdf");
    if (!options.as_object().empty()) {
      throw pdfkit::platform::Error(
          pdfkit::platform::ErrorCode::invalid_argument,
          "pdfkit_write options_json names the option \"" + options.as_object().front().first +
              "\", but write takes none",
          "an empty JSON object or NULL", "pass NULL or {} as options_json");
    }
    pdfkit::redact::WritePipeline::Output output =
        pdfkit::redact::WritePipeline::run(*handle.plan);
    std::string report = output.report.json();
    pdfkit::api::write_buffer(out_pdf, std::move(output.pdf));
    pdfkit::api::write_buffer(out, std::move(report));
    return PDFKIT_OK;
  });
}
