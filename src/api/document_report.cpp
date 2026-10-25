// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The shared members of the open report and the inspect output. Keys are
// written in a fixed order so two runs over one file give the same bytes.

#include "api/document_report.hpp"

#include <string>

namespace pdfkit::api {

void write_encryption(platform::JsonWriter& writer, const syntax::Document& document) {
  if (!document.security()) {
    writer.null();
    return;
  }
  const syntax::SecurityHandler::Description& description = document.security()->description();
  writer.begin_object();
  writer.key("filter").string("Standard");
  writer.key("version").integer(description.version);
  writer.key("revision").integer(description.revision);
  writer.key("method").string(description.method);
  writer.key("key_bits").integer(description.key_bits);
  writer.key("encrypt_metadata").boolean(description.encrypt_metadata);
  writer.end_object();
}

void write_repairs(platform::JsonWriter& writer, const syntax::Document& document) {
  writer.begin_array();
  for (const std::string& reason : document.repairs()) {
    writer.string(reason);
  }
  writer.end_array();
}

void write_warnings(platform::JsonWriter& writer, const syntax::Document& document) {
  writer.begin_array();
  if (!document.repairs().empty()) {
    writer.begin_object();
    writer.key("code").string("repaired");
    writer.key("message").string(
        "the file needed " + std::to_string(document.repairs().size()) +
        (document.repairs().size() == 1 ? " recovery" : " recoveries") +
        " to be read; an extraction from it may differ from what its producer wrote");
    writer.key("page").null();
    writer.key("reasons");
    write_repairs(writer, document);
    writer.end_object();
  }
  for (const syntax::Document::Warning& warning : document.warnings()) {
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
}

}  // namespace pdfkit::api
