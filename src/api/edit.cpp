// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The edit plan's lifecycle: pdfkit_edit_new creates an empty plan over an
// open document and pdfkit_edit_free releases it (docs/ABI.md, the lifecycle
// section). The plan holds what the editing functions record until
// pdfkit_write applies it.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"

#include <memory>

extern "C" PDFKIT_API pdfkit_status pdfkit_edit_new(pdfkit_document* doc, pdfkit_edit** out_edit,
                                                    pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&] {
    pdfkit::api::require_out(out, "pdfkit_edit_new", "out");
    pdfkit_document& document = pdfkit::api::require_document(doc, "pdfkit_edit_new");
    if (out_edit == nullptr) {
      throw pdfkit::platform::Error(pdfkit::platform::ErrorCode::invalid_argument,
                                    "pdfkit_edit_new was called with a null out_edit",
                                    "a pointer to a pdfkit_edit* that receives the new handle",
                                    "pass the address of a pdfkit_edit* variable");
    }
    *out_edit = nullptr;
    std::unique_ptr<pdfkit_edit> edit = pdfkit::api::new_edit(document);
    pdfkit::api::write_buffer(out, "{}");
    // Ownership passes to the caller until pdfkit_edit_free.
    *out_edit = edit.release();
    return PDFKIT_OK;
  });
}

extern "C" PDFKIT_API void pdfkit_edit_free(pdfkit_edit* edit) {
  (void)pdfkit::api::guarded(nullptr, [&] {
    // Takes back the handle pdfkit_edit_new handed over.
    const std::unique_ptr<pdfkit_edit> owned(edit);
    return PDFKIT_OK;
  });
}
