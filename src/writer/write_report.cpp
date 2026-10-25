// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The write report's JSON, through the platform's JSON writer.

#include "writer/write_report.hpp"

#include "platform/json_writer.hpp"

#include <cmath>

namespace pdfkit::writer {

std::string WriteReport::json() const {
  platform::JsonWriter writer;
  writer.begin_object();
  writer.key("page_count").unsigned_integer(pages.size());
  writer.key("pages").begin_array();
  for (const std::uint32_t page : pages) {
    writer.unsigned_integer(page);
  }
  writer.end_array();
  writer.key("bytes").unsigned_integer(bytes);
  writer.key("sha256").string(sha256);
  writer.key("objects").unsigned_integer(objects);
  writer.key("channels").begin_array();
  for (const ChannelTable::Count& count : channels) {
    writer.begin_object();
    writer.key("id").string(count.id);
    writer.key("stripped").unsigned_integer(count.count);
    writer.end_object();
  }
  writer.end_array();
  writer.key("pending_redactions").unsigned_integer(pending_redactions);
  writer.key("missing_references").unsigned_integer(missing_references);
  writer.key("unpruned_resources").unsigned_integer(unpruned_resources);
  writer.key("rectangles").begin_array();
  for (const Rectangles& changed : rectangles) {
    writer.begin_object();
    writer.key("page").unsigned_integer(changed.page);
    writer.key("source_page").unsigned_integer(changed.source_page);
    writer.key("placeholders").unsigned_integer(changed.placeholders);
    writer.key("boxes").begin_array();
    for (const std::array<double, 4>& box : changed.boxes) {
      writer.begin_array();
      for (const double value : box) {
        writer.number(std::round(value * 100.0) / 100.0);
      }
      writer.end_array();
    }
    writer.end_array();
    writer.end_object();
  }
  writer.end_array();
  writer.key("self_check").begin_object();
  writer.key("status").string(self_check_status);
  if (!self_check_reason.empty()) {
    writer.key("reason").string(self_check_reason);
  }
  if (self_check_status != "not_run") {
    writer.key("strings").unsigned_integer(self_check_strings);
  }
  writer.end_object();
  writer.key("warnings").begin_array();
  for (const ChannelTable::Warning& warning : warnings) {
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
  return writer.take();
}

}  // namespace pdfkit::writer
