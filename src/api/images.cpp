// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 3.1, List images: pdfkit_images lists every image XObject
// and inline image the asked pages draw, by identity, with its encoding,
// size, colour space, bits per component, mask and occurrences (page, ordinal
// in the page's draw order through forms, device box, form path), and a
// warning per image that does not decode (undecodable_image) or is reached
// only inside a pattern or a soft mask (image_in_pattern) (CLAUDE.md section
// 2, docs/ABI.md section 4.10). The only option is "pages"; a page out of
// range is invalid_argument. The draws come from the one content interpreter
// (CLAUDE.md section 3.2) through images::ImageListing, which the handle keeps.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "images/image_info.hpp"
#include "images/image_listing.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using pdfkit::images::ImageListing;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;
using pdfkit::platform::JsonWriter;

constexpr std::string_view shape =
    R"(an options object with at most "pages", an array of page numbers from 1)";

std::vector<std::uint32_t> read_pages(const JsonValue& input, std::size_t page_count) {
  for (const auto& [name, value] : input.as_object()) {
    if (name != "pages") {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_images options_json names the unknown option \"" + name + "\"",
                  std::string(shape), "remove the option; only pages is read");
    }
  }
  std::vector<std::uint32_t> pages;
  const JsonValue* list = input.find("pages");
  if (list == nullptr) {
    for (std::size_t i = 1; i <= page_count; ++i) {
      pages.push_back(static_cast<std::uint32_t>(i));
    }
    return pages;
  }
  if (list->kind() != JsonValue::Kind::array) {
    throw Error(ErrorCode::invalid_argument,
                "pdfkit_images options_json gives \"pages\" as " +
                    std::string(JsonValue::kind_name(list->kind())),
                std::string(shape), "list the pages as an array of page numbers, such as [1, 3]");
  }
  std::set<std::int64_t> seen;
  for (const JsonValue& item : list->as_array()) {
    const std::optional<std::int64_t> number =
        item.kind() == JsonValue::Kind::number ? item.as_int64() : std::nullopt;
    if (!number || *number < 1 || std::cmp_greater(*number, page_count)) {
      throw Error(
          ErrorCode::invalid_argument,
          "pdfkit_images options_json asks for page " +
              (number ? std::to_string(*number) : std::string("that is not a whole number")) +
              " of a document of " + std::to_string(page_count) + " pages",
          "page numbers from 1 to " + std::to_string(page_count),
          "ask only for pages the document has; pdfkit_inspect gives the page count");
    }
    if (!seen.insert(*number).second) {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_images options_json lists page " + std::to_string(*number) + " twice",
                  "each page at most once", "remove the repeated page number");
    }
    pages.push_back(static_cast<std::uint32_t>(*number));
  }
  return pages;
}

void optional_integer(JsonWriter& writer, std::string_view key,
                      const std::optional<std::int64_t>& value) {
  writer.key(key);
  if (value) {
    writer.integer(*value);
  } else {
    writer.null();
  }
}

// One image as listed: its entry, its occurrences on the asked pages and
// the asked pages it is reached on inside a pattern.
struct Listed {
  ImageListing::Entry* entry = nullptr;
  std::vector<const ImageListing::Occurrence*> occurrences;
  std::vector<std::uint32_t> pattern_pages;
  // The sort key: first page, a draw before a pattern, ordinal, identity.
  std::tuple<std::uint32_t, int, std::size_t, std::string> key;
};

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_images(pdfkit_document* doc, const char* options_json,
                                                  pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_images", "out");
    pdfkit_document& handle = pdfkit::api::require_document(doc, "pdfkit_images");
    const JsonValue input =
        pdfkit::api::parse_json_object(options_json, "pdfkit_images", "options_json", false);
    pdfkit::syntax::Document& document = *handle.document;
    const std::vector<std::uint32_t> pages = read_pages(input, document.pages().size());
    if (!handle.listing) {
      handle.listing = std::make_unique<ImageListing>(document);
    }
    handle.listing->read(pages);
    const std::set<std::uint32_t> asked(pages.begin(), pages.end());

    std::vector<Listed> listed;
    for (const ImageListing::Entry& constant : handle.listing->entries()) {
      ImageListing::Entry* entry = handle.listing->find(constant.identity);
      Listed item;
      item.entry = entry;
      for (const ImageListing::Occurrence& occurrence : entry->occurrences) {
        if (asked.contains(occurrence.page)) {
          item.occurrences.push_back(&occurrence);
        }
      }
      for (const std::uint32_t page : entry->pattern_pages) {
        if (asked.contains(page)) {
          item.pattern_pages.push_back(page);
        }
      }
      if (item.occurrences.empty() && item.pattern_pages.empty()) {
        continue;
      }
      if (!item.occurrences.empty() &&
          (item.pattern_pages.empty() ||
           item.occurrences.front()->page <= item.pattern_pages.front())) {
        item.key = {item.occurrences.front()->page, 0, item.occurrences.front()->ordinal,
                    entry->identity};
      } else {
        item.key = {item.pattern_pages.front(), 1, 0, entry->identity};
      }
      listed.push_back(std::move(item));
    }
    std::ranges::stable_sort(listed,
                             [](const Listed& a, const Listed& b) { return a.key < b.key; });

    struct Warning {
      std::string code;
      std::string message;
      std::uint32_t page = 0;
    };
    std::vector<Warning> warnings;
    JsonWriter writer;
    writer.begin_object();
    writer.key("pages").begin_array();
    for (const std::uint32_t page : pages) {
      writer.unsigned_integer(page);
    }
    writer.end_array();
    writer.key("count").unsigned_integer(listed.size());
    writer.key("images").begin_array();
    for (const Listed& item : listed) {
      ImageListing::Entry& entry = *item.entry;
      handle.listing->probe(entry);
      const bool inline_image = entry.source == ImageListing::Source::inline_image;
      const pdfkit::images::ImageInfo info =
          inline_image
              ? pdfkit::images::ImageInfo::of(document, *entry.dictionary.dictionary(),
                                              entry.colour_spaces)
              : pdfkit::images::ImageInfo::of(document, entry.stream.stream()->dictionary());
      const std::uint32_t first_page = std::get<0>(item.key);
      writer.begin_object();
      writer.key("identity").string(entry.identity);
      writer.key("source").string(inline_image ? "inline" : "xobject");
      writer.key("object");
      if (inline_image || entry.object_number == 0) {
        writer.null();
      } else {
        writer.unsigned_integer(entry.object_number);
      }
      writer.key("encoding").string(info.encoding);
      writer.key("filters").begin_array();
      for (const std::string& filter : info.filters) {
        writer.string(filter);
      }
      writer.end_array();
      optional_integer(writer, "width", info.width);
      optional_integer(writer, "height", info.height);
      optional_integer(writer, "bits_per_component", info.bits_per_component);
      writer.key("colour_space");
      if (info.colour_space.empty()) {
        writer.null();
      } else {
        writer.string(info.colour_space);
      }
      writer.key("components");
      if (info.components) {
        writer.unsigned_integer(*info.components);
      } else {
        writer.null();
      }
      writer.key("image_mask").boolean(info.image_mask);
      writer.key("mask").string(info.mask);
      writer.key("interpolate").boolean(info.interpolate);
      writer.key("decodable").boolean(entry.fault.empty());
      writer.key("warnings").begin_array();
      if (!entry.fault.empty()) {
        writer.string("undecodable_image");
        warnings.push_back({"undecodable_image",
                            "the image " + entry.identity + " (" + info.encoding +
                                ") does not decode: " + entry.fault +
                                "; only keep and remove apply to it",
                            first_page});
      }
      if (!item.pattern_pages.empty()) {
        writer.string("image_in_pattern");
        warnings.push_back({"image_in_pattern",
                            "the image " + entry.identity +
                                " is drawn inside a pattern or a soft mask on page " +
                                std::to_string(item.pattern_pages.front()) +
                                "; it is listed there but its draws there are not numbered",
                            item.pattern_pages.front()});
      }
      writer.end_array();
      writer.key("occurrences").begin_array();
      for (const ImageListing::Occurrence* occurrence : item.occurrences) {
        writer.begin_object();
        writer.key("page").unsigned_integer(occurrence->page);
        writer.key("ordinal").unsigned_integer(occurrence->ordinal);
        writer.key("box").begin_array();
        writer.fixed(occurrence->box.left, 2);
        writer.fixed(occurrence->box.bottom, 2);
        writer.fixed(occurrence->box.right, 2);
        writer.fixed(occurrence->box.top, 2);
        writer.end_array();
        writer.key("form_path").begin_array();
        for (const std::uint32_t form : occurrence->form_path) {
          writer.unsigned_integer(form);
        }
        writer.end_array();
        writer.key("optional_content_off").boolean(occurrence->optional_content_off);
        writer.end_object();
      }
      writer.end_array();
      writer.key("pattern_pages").begin_array();
      for (const std::uint32_t page : item.pattern_pages) {
        writer.unsigned_integer(page);
      }
      writer.end_array();
      writer.end_object();
    }
    writer.end_array();
    writer.key("warnings").begin_array();
    for (const Warning& warning : warnings) {
      writer.begin_object();
      writer.key("code").string(warning.code);
      writer.key("message").string(warning.message);
      writer.key("page").unsigned_integer(warning.page);
      writer.end_object();
    }
    writer.end_array();
    writer.end_object();
    pdfkit::api::write_buffer(out, writer.take());
    return PDFKIT_OK;
  });
}
