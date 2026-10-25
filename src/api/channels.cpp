// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 8, Strip hidden channels: pdfkit_channels reports, per
// row of the channel table, whether the channel is present in the document
// and its count, the /Redact annotations never applied, and their warnings
// (CLAUDE.md section 2, docs/ABI.md). Every pdfkit_write strips the same
// rows. It takes no options and never fails on a valid handle.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "writer/channel_table.hpp"
#include "writer/object_graph.hpp"

#include <string>

extern "C" PDFKIT_API pdfkit_status pdfkit_channels(pdfkit_document* doc, const char* options_json,
                                                    pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_channels", "out");
    pdfkit_document& handle = pdfkit::api::require_document(doc, "pdfkit_channels");
    const pdfkit::platform::JsonValue options =
        pdfkit::api::parse_json_object(options_json, "pdfkit_channels", "options_json", false);
    if (!options.as_object().empty()) {
      throw pdfkit::platform::Error(
          pdfkit::platform::ErrorCode::invalid_argument,
          "pdfkit_channels options_json names the option \"" + options.as_object().front().first +
              "\", but channels takes none",
          "an empty JSON object or NULL", "pass NULL or {} as options_json");
    }
    pdfkit::writer::ObjectGraph graph(*handle.document);
    const pdfkit::writer::ChannelTable::Report report = pdfkit::writer::ChannelTable::detect(graph);
    const auto rows = pdfkit::writer::ChannelTable::rows();
    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("channels").begin_array();
    for (std::size_t i = 0; i < rows.size(); ++i) {
      writer.begin_object();
      writer.key("id").string(rows[i].id);
      writer.key("name").string(rows[i].name);
      writer.key("present").boolean(report.counts[i].count > 0);
      writer.key("count").unsigned_integer(report.counts[i].count);
      writer.end_object();
    }
    writer.end_array();
    writer.key("present").begin_array();
    for (const auto& count : report.counts) {
      if (count.count > 0) {
        writer.string(count.id);
      }
    }
    writer.end_array();
    writer.key("pending_redactions").unsigned_integer(report.pending_redactions);
    writer.key("warnings").begin_array();
    for (const auto& warning : report.warnings) {
      writer.begin_object();
      writer.key("code").string(warning.code);
      writer.key("message").string(warning.message);
      writer.key("page");
      if (warning.page) {
        writer.unsigned_integer(*warning.page);
      } else {
        writer.null();
      }
      writer.end_object();
    }
    writer.end_array();
    writer.end_object();
    pdfkit::api::write_buffer(out, writer.take());
    return PDFKIT_OK;
  });
}
