// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 4, Extract metadata: pdfkit_metadata returns the Info
// dictionary and the XMP packet side by side, one row per field of
// docs/DESIGN.md section 10, with whether the two agree (CLAUDE.md section 2,
// docs/ABI.md). It takes no options and never fails on a valid handle: a
// packet that does not scan is reported as unreadable with its reason and
// the warning xmp_unreadable.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "syntax/metadata_reader.hpp"
#include "syntax/pdf_doc_encoding.hpp"

#include <optional>
#include <string>

namespace {

using pdfkit::platform::JsonWriter;
using pdfkit::syntax::MetadataReader;

void optional_string(JsonWriter& writer, const std::optional<std::string>& value) {
  if (value) {
    writer.string(*value);
  } else {
    writer.null();
  }
}

std::string json(const MetadataReader::Result& result) {
  JsonWriter writer;
  writer.begin_object();
  writer.key("info").boolean(result.info_present);
  writer.key("xmp").string(!result.xmp_present   ? "absent"
                           : result.xmp_readable ? "read"
                                                 : "unreadable");
  writer.key("xmp_fault");
  if (result.xmp_present && !result.xmp_readable) {
    writer.string(result.xmp_fault);
  } else {
    writer.null();
  }
  bool agree = true;
  for (const MetadataReader::Row& row : result.rows) {
    agree = agree && row.agree.value_or(true);
  }
  writer.key("agree").boolean(agree);
  writer.key("utf16le").unsigned_integer(result.utf16le);
  writer.key("fields").begin_array();
  for (const MetadataReader::Row& row : result.rows) {
    writer.begin_object();
    writer.key("field").string(MetadataReader::name(row.field));
    writer.key("info");
    optional_string(writer, row.info);
    writer.key("info_encoding");
    if (row.info) {
      writer.string(pdfkit::syntax::PdfDocEncoding::name(row.info_form));
    } else {
      writer.null();
    }
    writer.key("xmp");
    if (row.xmp.empty()) {
      writer.null();
    } else {
      writer.string(row.xmp.front().value);
    }
    writer.key("xmp_property");
    if (row.xmp.empty()) {
      writer.null();
    } else {
      writer.string(row.xmp.front().property);
    }
    writer.key("xmp_values").begin_array();
    for (const MetadataReader::XmpValue& value : row.xmp) {
      writer.begin_object();
      writer.key("property").string(value.property);
      writer.key("value").string(value.value);
      writer.end_object();
    }
    writer.end_array();
    if (MetadataReader::is_date(row.field)) {
      writer.key("info_iso");
      optional_string(writer, row.info_iso);
      writer.key("xmp_iso");
      optional_string(writer, row.xmp_iso);
    }
    writer.key("agree");
    if (row.agree) {
      writer.boolean(*row.agree);
    } else {
      writer.null();
    }
    writer.end_object();
  }
  writer.end_array();
  writer.key("warnings").begin_array();
  if (result.xmp_present && !result.xmp_readable) {
    writer.begin_object();
    writer.key("code").string("xmp_unreadable");
    writer.key("message").string("the catalog's XMP packet does not scan (" + result.xmp_fault +
                                 "), so it is not read and is dropped from any output");
    writer.key("page").null();
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
  return writer.take();
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_metadata(pdfkit_document* doc, const char* options_json,
                                                    pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_metadata", "out");
    pdfkit_document& handle = pdfkit::api::require_document(doc, "pdfkit_metadata");
    const pdfkit::platform::JsonValue options =
        pdfkit::api::parse_json_object(options_json, "pdfkit_metadata", "options_json", false);
    if (!options.as_object().empty()) {
      throw pdfkit::platform::Error(
          pdfkit::platform::ErrorCode::invalid_argument,
          "pdfkit_metadata options_json names the option \"" + options.as_object().front().first +
              "\", but metadata takes none",
          "an empty JSON object or NULL", "pass NULL or {} as options_json");
    }
    const MetadataReader::Result result = MetadataReader::read(*handle.document);
    pdfkit::api::write_buffer(out, json(result));
    return PDFKIT_OK;
  });
}
