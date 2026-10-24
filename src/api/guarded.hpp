// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// guarded is the catch-all every exported body is one call into: it runs the
// body, maps a platform::Error to its status and JSON error object, maps any
// other exception to PDFKIT_E_INTERNAL, and never lets an exception escape,
// because one unwinding into P/Invoke or ctypes terminates the caller's
// process (CLAUDE.md section 3.3). The buffer helpers here are the one way a
// result reaches a pdfkit_buffer.

#ifndef PDFKIT_API_GUARDED_HPP
#define PDFKIT_API_GUARDED_HPP

#include "pdfkit/pdfkit.h"

#include "platform/error.hpp"

#include <string>

namespace pdfkit::api {

// The status a code maps to.
[[nodiscard]] pdfkit_status status_of(platform::ErrorCode code) noexcept;

// The JSON error object {"code", "failed", "expected", "fix"}.
[[nodiscard]] std::string error_json(const platform::Error& error);

// Hands bytes to the caller through out, which must not be null.
void write_buffer(pdfkit_buffer* out, std::string bytes);

// Maps the exception being handled to a status, writing its error object into
// out when out is not null. Called only from a catch block.
[[nodiscard]] pdfkit_status report_current_exception(pdfkit_buffer* out) noexcept;

// Runs body, which returns a pdfkit_status, and turns every exception into a
// status. out may be null, for the calls that have no buffer.
template <typename Body>
[[nodiscard]] pdfkit_status guarded(pdfkit_buffer* out, Body&& body) noexcept {
  try {
    return body();
  } catch (...) {
    return report_current_exception(out);
  }
}

}  // namespace pdfkit::api

#endif  // PDFKIT_API_GUARDED_HPP
