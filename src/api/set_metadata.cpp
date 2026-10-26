// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 7, Rewrite metadata: pdfkit_set_metadata records the
// field values an edit plan writes into both the Info dictionary and the XMP
// packet (CLAUDE.md section 2, docs/DESIGN.md section 10, docs/ABI.md). The
// fields are one JSON object keyed by the Info names of the metadata field
// list, each a string or null (null removes the field from both places); a
// key outside the list is refused, naming the list. The answer is every field
// the plan holds as it will be written, with the XMP properties each touches.
// The source's values of each field set are kept with the plan, because the
// self-check searches the output for every one of them.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "redact/edit_plan.hpp"
#include "syntax/metadata_reader.hpp"
#include "writer/metadata_writer.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;
using pdfkit::syntax::MetadataReader;
using pdfkit::writer::MetadataWriter;

std::string field_list() {
  std::string out;
  for (const MetadataReader::Field field : MetadataReader::fields) {
    out += (out.empty() ? "" : ", ") + std::string(MetadataReader::name(field));
  }
  return out;
}

// The shortest original the self-check searches for, in letters and digits:
// a value of one or two (an initial, "PC", "1") reads as part of any text, so
// searching for it would refuse every output rather than find a leak.
constexpr std::size_t shortest_original = 3;

void add_original(std::vector<std::string>& originals, const std::string& value) {
  std::size_t letters = 0;
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    // ASCII letters and digits, and the lead byte of every other character.
    letters += (std::isalnum(byte) != 0 || byte >= 0xC0) ? 1 : 0;
  }
  if (letters >= shortest_original &&
      std::find(originals.begin(), originals.end(), value) == originals.end()) {
    originals.push_back(value);
  }
}

std::string json(const pdfkit::redact::EditPlan& plan, const MetadataReader::Result& source) {
  pdfkit::platform::JsonWriter writer;
  writer.begin_object();
  writer.key("fields").begin_array();
  const std::vector<MetadataWriter::Written> written = MetadataWriter::preview(plan.metadata());
  for (const MetadataWriter::Written& field : written) {
    writer.begin_object();
    writer.key("field").string(MetadataReader::name(field.field));
    writer.key("removed").boolean(field.removed);
    writer.key("info");
    if (field.removed) {
      writer.null();
    } else {
      writer.string(field.info);
    }
    writer.key("info_encoding");
    if (field.removed) {
      writer.null();
    } else {
      writer.string(field.info_encoding);
    }
    writer.key("xmp");
    if (field.removed || !source.xmp_states) {
      writer.null();
    } else {
      writer.string(field.xmp);
    }
    // The properties the write touches: those the packet holds, or the
    // first the field pairs with, added, when it holds none.
    writer.key("xmp_properties").begin_array();
    if (source.xmp_states) {
      const MetadataReader::Row& row = source.rows[static_cast<std::size_t>(field.field)];
      for (const MetadataReader::XmpValue& value : row.xmp) {
        writer.string(value.property);
      }
      if (row.xmp.empty() && !field.removed) {
        writer.string(MetadataReader::pairs(field.field).front().name);
      }
    }
    writer.end_array();
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
  return writer.take();
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_set_metadata(pdfkit_edit* edit, const char* fields_json,
                                                        pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_set_metadata", "out");
    pdfkit_edit& handle = pdfkit::api::require_edit(edit, "pdfkit_set_metadata");
    const JsonValue fields =
        pdfkit::api::parse_json_object(fields_json, "pdfkit_set_metadata", "fields_json", true);
    const std::string expected =
        "a JSON object whose keys are fields of the metadata field list (" + field_list() +
        "), each a string or null";
    MetadataWriter::Values values;
    for (const auto& [key, value] : fields.as_object()) {
      const std::optional<MetadataReader::Field> field = MetadataReader::field(key);
      if (!field) {
        throw Error(ErrorCode::invalid_argument,
                    "pdfkit_set_metadata fields_json names the field \"" + key +
                        "\", which is not in the metadata field list",
                    expected,
                    "remove \"" + key + "\" or use one of " + field_list() +
                        " (the names are case-sensitive)");
      }
      if (value.kind() == JsonValue::Kind::null) {
        values[*field] = std::nullopt;
      } else if (value.kind() == JsonValue::Kind::string) {
        values[*field] = value.as_string();
      } else {
        throw Error(ErrorCode::invalid_argument,
                    "pdfkit_set_metadata fields_json gives the field \"" + key + "\" as " +
                        std::string(JsonValue::kind_name(value.kind())),
                    expected, "write \"" + key + "\" as a string, or null to remove it");
      }
    }
    pdfkit::redact::EditPlan& plan = *handle.plan;
    const MetadataReader::Result source = MetadataReader::read(plan.document());
    for (const auto& [field, value] : values) {
      plan.metadata()[field] = value;
      const MetadataReader::Row& row = source.rows[static_cast<std::size_t>(field)];
      if (row.info) {
        add_original(plan.metadata_originals(), *row.info);
      }
      for (const MetadataReader::XmpValue& xmp : row.xmp) {
        add_original(plan.metadata_originals(), xmp.value);
        for (const std::string& item : xmp.items) {
          add_original(plan.metadata_originals(), item);
        }
      }
    }
    pdfkit::api::write_buffer(out, json(plan, source));
    return PDFKIT_OK;
  });
}
