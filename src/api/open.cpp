// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 1.1, Open: pdfkit_open reads PDF bytes into a document
// handle, repairing what it can and decrypting with an empty user password,
// and returns the open report (CLAUDE.md section 2, docs/ABI.md). The options
// are the limits of docs/DESIGN.md section 3.2; an unknown option is refused,
// because a misspelt limit that silently keeps its default is a limit the
// caller believes raised.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/document_report.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/byte_span.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "syntax/document.hpp"
#include "syntax/limits.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;

constexpr std::array<std::string_view, 5> option_names{"stream_bytes", "document_bytes", "objects",
                                                       "page_ceiling", "operators_per_page"};

std::string accepted_options() {
  std::string out;
  for (const std::string_view name : option_names) {
    out += out.empty() ? "" : ", ";
    out += name;
  }
  return out;
}

pdfkit::syntax::Limits limits_of(const JsonValue& options) {
  pdfkit::syntax::Limits limits;
  for (const auto& [key, value] : options.as_object()) {
    const auto number = value.kind() == JsonValue::Kind::number ? value.as_int64() : std::nullopt;
    const bool known = std::ranges::find(option_names, key) != option_names.end();
    if (!known) {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_open options_json names the unknown option \"" + key + "\"",
                  "only the options " + accepted_options(),
                  "remove the option or correct its spelling (docs/ABI.md, pdfkit_open)");
    }
    if (!number || *number < 1) {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_open options_json: \"" + key + "\" is not a positive integer",
                  "a whole number of at least 1",
                  "give \"" + key + "\" as a positive integer, or leave it out for its default");
    }
    const auto amount = static_cast<std::uint64_t>(*number);
    if (key == "stream_bytes") {
      limits.stream_bytes = amount;
    } else if (key == "document_bytes") {
      limits.document_bytes = amount;
    } else if (key == "objects") {
      limits.objects = amount;
    } else if (key == "page_ceiling") {
      limits.page_ceiling = amount;
    } else {
      limits.operators_per_page = amount;
    }
  }
  return limits;
}

std::string open_report(const pdfkit::syntax::Document& document) {
  pdfkit::platform::JsonWriter writer;
  writer.begin_object();
  writer.key("version").string(document.version());
  writer.key("page_count").unsigned_integer(document.pages().size());
  writer.key("encryption");
  pdfkit::api::write_encryption(writer, document);
  writer.key("repairs");
  pdfkit::api::write_repairs(writer, document);
  writer.key("warnings");
  pdfkit::api::write_warnings(writer, document);
  writer.end_object();
  return writer.take();
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_open(const std::uint8_t* bytes, std::size_t size,
                                                const char* options_json, pdfkit_document** out_doc,
                                                pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_open", "out");
    pdfkit::api::require_bytes(bytes, size, "pdfkit_open");
    const JsonValue options =
        pdfkit::api::parse_json_object(options_json, "pdfkit_open", "options_json", false);
    if (out_doc == nullptr) {
      throw Error(ErrorCode::invalid_argument, "pdfkit_open was called with a null out_doc",
                  "a pointer to a pdfkit_document* that receives the handle",
                  "pass the address of a pdfkit_document* variable");
    }
    *out_doc = nullptr;
    const pdfkit::syntax::Limits limits = limits_of(options);
    const pdfkit::platform::ByteSpan input =
        size == 0 ? pdfkit::platform::ByteSpan()
                  : std::as_bytes(std::span<const std::uint8_t>(bytes, size));
    std::unique_ptr<pdfkit::syntax::Document> document =
        pdfkit::syntax::Document::open(input, limits);
    std::string report = open_report(*document);
    std::unique_ptr<pdfkit_document> handle = pdfkit::api::new_document(std::move(document));
    pdfkit::api::write_buffer(out, std::move(report));
    // Ownership passes to the caller until pdfkit_close.
    *out_doc = handle.release();
    return PDFKIT_OK;
  });
}
