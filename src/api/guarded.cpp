// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The status mapping, the error object and the buffer hand-over of the C ABI.
// The buffer's storage is a std::string, whose terminating zero gives the
// caller a C string for free; its ownership crosses the ABI as the reserved
// pointer and comes back in pdfkit_buffer_free.

#include "api/guarded.hpp"

#include "platform/json_writer.hpp"

#include <memory>
#include <new>
#include <utility>

namespace pdfkit::api {

namespace {

constexpr const char* report_fix =
    "this is a defect in pdfkit; report it with the input bytes and the options that caused it";

// Writes an internal error; a failure while writing it leaves out untouched.
void write_internal(pdfkit_buffer* out, const char* failed) noexcept {
  if (out == nullptr) {
    return;
  }
  try {
    write_buffer(out, error_json(platform::Error(
                          platform::ErrorCode::internal, failed,
                          "the call to complete, or to refuse its input with a documented code",
                          report_fix)));
  } catch (...) {
    // Out of memory while reporting: the status alone still reaches the caller.
    return;
  }
}

}  // namespace

pdfkit_status status_of(platform::ErrorCode code) noexcept {
  switch (code) {
    case platform::ErrorCode::invalid_argument:
      return PDFKIT_E_INVALID_ARGUMENT;
    case platform::ErrorCode::self_check:
      return PDFKIT_E_SELF_CHECK;
    case platform::ErrorCode::not_implemented:
      return PDFKIT_E_NOT_IMPLEMENTED;
    case platform::ErrorCode::internal:
      return PDFKIT_E_INTERNAL;
    case platform::ErrorCode::not_pdf:
    case platform::ErrorCode::no_page:
    case platform::ErrorCode::password_required:
    case platform::ErrorCode::unsupported_security_handler:
    case platform::ErrorCode::limit_exceeded:
    case platform::ErrorCode::page_ceiling:
      return PDFKIT_E_REFUSED;
  }
  return PDFKIT_E_INTERNAL;
}

std::string error_json(const platform::Error& error) {
  platform::JsonWriter writer;
  writer.begin_object();
  writer.key("code").string(platform::error_code_name(error.code()));
  writer.key("failed").string(error.failed());
  writer.key("expected").string(error.expected());
  writer.key("fix").string(error.fix());
  writer.end_object();
  return writer.take();
}

void write_buffer(pdfkit_buffer* out, std::string bytes) {
  auto storage = std::make_unique<std::string>(std::move(bytes));
  out->data = reinterpret_cast<const std::uint8_t*>(storage->data());
  out->size = storage->size();
  // Ownership passes to the caller until pdfkit_buffer_free takes it back.
  out->reserved = storage.release();
}

pdfkit_status report_current_exception(pdfkit_buffer* out) noexcept {
  try {
    throw;
  } catch (const platform::Error& error) {
    if (out != nullptr) {
      try {
        write_buffer(out, error_json(error));
      } catch (...) {
        write_internal(out, "pdfkit ran out of memory while reporting an error");
      }
    }
    return status_of(error.code());
  } catch (const std::bad_alloc&) {
    write_internal(out, "pdfkit ran out of memory");
  } catch (const std::exception& error) {
    write_internal(out, error.what());
  } catch (...) {
    write_internal(out, "pdfkit raised an exception of an unknown type");
  }
  return PDFKIT_E_INTERNAL;
}

}  // namespace pdfkit::api
