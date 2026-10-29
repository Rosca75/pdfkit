// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The lifecycle symbols of the C ABI: the ABI and library versions, freeing a
// buffer and closing a document. They belong to no catalogue row, and they
// work in every build; the edit plan's lifecycle is in edit.cpp.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/build_info.hpp"
#include "platform/export.hpp"
#include "platform/json_writer.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace pdfkit::api {

namespace {

// Raised on every addition to the ABI and on a change of meaning the owner
// orders (docs/ABI.md, Versioning).
constexpr std::uint32_t abi_version = 2;

std::string version_json() {
  platform::JsonWriter writer;
  writer.begin_object();
  writer.key("version").string(platform::library_version());
  writer.key("abi").unsigned_integer(abi_version);
  writer.key("build").string(platform::build_description());
  writer.key("catalogue").begin_array();
  for (const Catalogue::Row& row : Catalogue::rows()) {
    const bool implemented = row.status == Catalogue::Status::implemented;
    writer.begin_object();
    writer.key("number").string(row.number);
    writer.key("function").string(row.function);
    writer.key("symbol").string(row.symbol);
    writer.key("verb").string(row.verb);
    writer.key("build_file").string(row.build_file);
    writer.key("implemented").boolean(implemented);
    if (!implemented) {
      const platform::Error error = Catalogue::not_implemented_error(row);
      writer.key("error").begin_object();
      writer.key("code").string(platform::error_code_name(error.code()));
      writer.key("failed").string(error.failed());
      writer.key("expected").string(error.expected());
      writer.key("fix").string(error.fix());
      writer.end_object();
    }
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
  return writer.take();
}

}  // namespace

}  // namespace pdfkit::api

extern "C" PDFKIT_API std::uint32_t pdfkit_abi_version() {
  return pdfkit::api::abi_version;
}

extern "C" PDFKIT_API pdfkit_status pdfkit_version(pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&] {
    pdfkit::api::require_out(out, "pdfkit_version", "out");
    pdfkit::api::write_buffer(out, pdfkit::api::version_json());
    return PDFKIT_OK;
  });
}

extern "C" PDFKIT_API void pdfkit_buffer_free(pdfkit_buffer* buffer) {
  (void)pdfkit::api::guarded(nullptr, [&] {
    if (buffer != nullptr) {
      // Takes back the storage write_buffer handed over.
      const std::unique_ptr<std::string> storage(static_cast<std::string*>(buffer->reserved));
      buffer->data = nullptr;
      buffer->size = 0;
      buffer->reserved = nullptr;
    }
    return PDFKIT_OK;
  });
}

extern "C" PDFKIT_API void pdfkit_close(pdfkit_document* doc) {
  (void)pdfkit::api::guarded(nullptr, [&] {
    // Takes back the handle pdfkit_open handed over.
    const std::unique_ptr<pdfkit_document> owned(doc);
    return PDFKIT_OK;
  });
}
