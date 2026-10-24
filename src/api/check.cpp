// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 10.2, Check: pdfkit_check searches any PDF bytes for surviving occurrences of
// strings under their folding rules (CLAUDE.md section 2, docs/ABI.md). This build checks the
// arguments and then returns the not-implemented error the catalogue registry names.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "platform/export.hpp"

#include <cstddef>
#include <cstdint>

extern "C" PDFKIT_API pdfkit_status pdfkit_check(const std::uint8_t* bytes, std::size_t size,
                                                 const char* strings_json, pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_check", "out");
    pdfkit::api::require_bytes(bytes, size, "pdfkit_check");
    (void)pdfkit::api::parse_json_object(strings_json, "pdfkit_check", "strings_json", true);
    pdfkit::api::Catalogue::throw_not_implemented("pdfkit_check");
  });
}
