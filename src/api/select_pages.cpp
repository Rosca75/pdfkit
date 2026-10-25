// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 9, Select pages: pdfkit_select_pages records on an edit
// plan the pages the output keeps, in the order given (CLAUDE.md section 2,
// docs/ABI.md); pdfkit_write applies it. The input is {"pages": [n, ...]}
// with 1-based page numbers; an empty list, a page named twice, a page out of
// range, or any other shape is refused with what was wrong and how to fix it.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;

constexpr const char* shape = "a JSON object {\"pages\": [1, 3, 4]} of 1-based page numbers";

std::vector<std::int64_t> pages_of(const JsonValue& input) {
  for (const auto& member : input.as_object()) {
    if (member.first != "pages") {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_select_pages pages_json names the unknown key \"" + member.first + "\"",
                  shape, "remove the key; only \"pages\" is read");
    }
  }
  const JsonValue* list = input.find("pages");
  if (list == nullptr || list->kind() != JsonValue::Kind::array) {
    throw Error(ErrorCode::invalid_argument,
                "pdfkit_select_pages pages_json has no \"pages\" array", shape,
                "give the pages to keep as an array of page numbers under \"pages\"");
  }
  std::vector<std::int64_t> pages;
  for (const JsonValue& item : list->as_array()) {
    const std::optional<std::int64_t> number =
        item.kind() == JsonValue::Kind::number ? item.as_int64() : std::nullopt;
    if (!number) {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_select_pages pages_json lists " +
                      std::string(JsonValue::kind_name(item.kind())) +
                      " where a page number belongs",
                  shape, "write each page as a whole number, such as 3");
    }
    pages.push_back(*number);
  }
  return pages;
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_select_pages(pdfkit_edit* edit, const char* pages_json,
                                                        pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_select_pages", "out");
    pdfkit_edit& handle = pdfkit::api::require_edit(edit, "pdfkit_select_pages");
    const JsonValue input =
        pdfkit::api::parse_json_object(pages_json, "pdfkit_select_pages", "pages_json", true);
    handle.plan->select_pages(pages_of(input));
    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("pages").begin_array();
    for (const std::uint32_t page : *handle.plan->pages()) {
      writer.unsigned_integer(page);
    }
    writer.end_array();
    writer.end_object();
    pdfkit::api::write_buffer(out, writer.take());
    return PDFKIT_OK;
  });
}
