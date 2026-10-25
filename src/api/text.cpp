// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 2, Extract text: pdfkit_text returns each page's blocks,
// lines and words in reading order and, on request, the glyph records behind
// them (CLAUDE.md section 2, docs/ABI.md section 4.5). Pages are read one at a
// time and written as they are read, holding only the page before, whose last
// line a hyphen may join to the next page's first line (docs/DESIGN.md
// chapter 7: across a page break only when both pages are extracted
// together). A page number outside the document is invalid_argument, checked
// before any page is read.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "text/page_text.hpp"
#include "text/text_json.hpp"
#include "text/text_reader.hpp"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;

constexpr std::string_view shape =
    "an options object with any of \"pages\" (an array of page numbers from 1), \"blocks\", "
    "\"glyphs\" and \"reserialisation\" (each true or false)";

struct Options {
  std::vector<std::uint32_t> pages;
  pdfkit::text::TextJson::Options json;
};

bool flag(const JsonValue& value, const std::string& name) {
  if (value.kind() != JsonValue::Kind::boolean) {
    throw Error(ErrorCode::invalid_argument,
                "pdfkit_text options_json gives \"" + name + "\" as " +
                    std::string(JsonValue::kind_name(value.kind())),
                std::string(shape), "write \"" + name + "\": true or \"" + name + "\": false");
  }
  return value.as_bool();
}

Options read_options(const JsonValue& input, std::size_t page_count) {
  Options options;
  for (const auto& [name, value] : input.as_object()) {
    if (name == "blocks") {
      options.json.blocks = flag(value, name);
    } else if (name == "glyphs") {
      options.json.glyphs = flag(value, name);
    } else if (name == "reserialisation") {
      options.json.reserialisation = flag(value, name);
    } else if (name != "pages") {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_text options_json names the unknown option \"" + name + "\"",
                  std::string(shape),
                  "remove the option; only pages, blocks, glyphs and reserialisation are read");
    }
  }
  const JsonValue* pages = input.find("pages");
  if (pages == nullptr) {
    for (std::size_t i = 1; i <= page_count; ++i) {
      options.pages.push_back(static_cast<std::uint32_t>(i));
    }
    return options;
  }
  if (pages->kind() != JsonValue::Kind::array) {
    throw Error(ErrorCode::invalid_argument,
                "pdfkit_text options_json gives \"pages\" as " +
                    std::string(JsonValue::kind_name(pages->kind())),
                std::string(shape), "list the pages as an array of page numbers, such as [1, 3]");
  }
  std::set<std::int64_t> seen;
  for (const JsonValue& item : pages->as_array()) {
    const std::optional<std::int64_t> number =
        item.kind() == JsonValue::Kind::number ? item.as_int64() : std::nullopt;
    if (!number || *number < 1 || static_cast<std::uint64_t>(*number) > page_count) {
      throw Error(
          ErrorCode::invalid_argument,
          "pdfkit_text options_json asks for page " +
              (number ? std::to_string(*number) : std::string("that is not a whole number")) +
              " of a document of " + std::to_string(page_count) + " pages",
          "page numbers from 1 to " + std::to_string(page_count),
          "ask only for pages the document has; pdfkit_inspect gives the page count");
    }
    if (!seen.insert(*number).second) {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_text options_json lists page " + std::to_string(*number) + " twice",
                  "each page at most once", "remove the repeated page number");
    }
    options.pages.push_back(static_cast<std::uint32_t>(*number));
  }
  return options;
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_text(pdfkit_document* doc, const char* options_json,
                                                pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_text", "out");
    pdfkit_document& handle = pdfkit::api::require_document(doc, "pdfkit_text");
    const JsonValue input =
        pdfkit::api::parse_json_object(options_json, "pdfkit_text", "options_json", false);
    pdfkit::syntax::Document& document = *handle.document;
    const Options options = read_options(input, document.pages().size());

    pdfkit::text::TextReader reader(document);
    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("pages").begin_array();
    std::optional<pdfkit::text::PageText> held;
    for (const std::uint32_t number : options.pages) {
      pdfkit::text::PageText page = reader.read(number);
      if (held) {
        if (held->page + 1 == page.page && !held->blocks.empty() && !page.blocks.empty() &&
            !held->blocks.back().lines.empty() && !page.blocks.front().lines.empty()) {
          pdfkit::text::TextLayout::join_hyphen(held->blocks.back().lines.back(),
                                                page.blocks.front().lines.front());
        }
        pdfkit::text::TextJson::write_page(writer, *held, options.json);
      }
      held = std::move(page);
    }
    if (held) {
      pdfkit::text::TextJson::write_page(writer, *held, options.json);
    }
    writer.end_array();
    writer.end_object();
    pdfkit::api::write_buffer(out, writer.take());
    return PDFKIT_OK;
  });
}
