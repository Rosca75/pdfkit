// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 1.2, Inspect: pdfkit_inspect reports a document's
// version, page count, page boxes and rotation, encryption, repairs and
// warnings (CLAUDE.md section 2, docs/ABI.md), and the pages whose text could
// not be read in full. It reads every page's content once to name them,
// keeping the answer on the handle, and a limit met there names the page
// not_checked, so it never fails on a valid handle; it takes no options.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/document_report.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "syntax/page.hpp"
#include "syntax/page_damage.hpp"

#include <string>
#include <vector>

namespace {

void write_box(pdfkit::platform::JsonWriter& writer, const pdfkit::syntax::Page::Box& box) {
  writer.begin_array();
  writer.number(box.left);
  writer.number(box.bottom);
  writer.number(box.right);
  writer.number(box.top);
  writer.end_array();
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_inspect(pdfkit_document* doc, const char* options_json,
                                                   pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_inspect", "out");
    pdfkit_document& handle = pdfkit::api::require_document(doc, "pdfkit_inspect");
    const pdfkit::platform::JsonValue options =
        pdfkit::api::parse_json_object(options_json, "pdfkit_inspect", "options_json", false);
    if (!options.as_object().empty()) {
      throw pdfkit::platform::Error(
          pdfkit::platform::ErrorCode::invalid_argument,
          "pdfkit_inspect options_json names the option \"" + options.as_object().front().first +
              "\", but inspect takes none",
          "an empty JSON object or NULL", "pass NULL or {} as options_json");
    }
    // Read first, so the warnings it meets are among those written.
    const std::vector<pdfkit::syntax::PageDamage> damaged = handle.damage->all();
    const pdfkit::syntax::Document& document = *handle.document;
    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("version").string(document.version());
    writer.key("page_count").unsigned_integer(document.pages().size());
    writer.key("pages").begin_array();
    for (const pdfkit::syntax::Page& page : document.pages()) {
      writer.begin_object();
      writer.key("page").unsigned_integer(page.number);
      writer.key("media_box");
      write_box(writer, page.media_box);
      writer.key("crop_box");
      write_box(writer, page.crop_box);
      writer.key("rotate").integer(page.rotate);
      writer.end_object();
    }
    writer.end_array();
    writer.key("encryption");
    pdfkit::api::write_encryption(writer, document);
    writer.key("repairs");
    pdfkit::api::write_repairs(writer, document);
    writer.key("warnings");
    pdfkit::api::write_warnings(writer, document);
    writer.key("damaged_pages");
    pdfkit::syntax::PageDamage::write(writer, damaged);
    writer.end_object();
    pdfkit::api::write_buffer(out, writer.take());
    return PDFKIT_OK;
  });
}
