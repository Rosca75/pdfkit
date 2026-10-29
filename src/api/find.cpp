// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 11, Find: pdfkit_find answers, for each value the
// caller gives with its folding, every place it occurs, in every order a
// reader reads a page, with the glyph ranges a located match of
// pdfkit_redact_text takes back, the device rectangles, the hidden classes
// and a few words of context (CLAUDE.md section 2, the pdfkit_find section of
// docs/ABI.md). It runs the one search the redaction runs
// (MatchFinder::locate), so the count shown to a user is the count a search
// of the same value removes. Pages are read one at a time, in page order,
// each once for every value; nothing is recorded on the document.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/folding_argument.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "platform/unicode/folding.hpp"
#include "redact/match_finder.hpp"
#include "redact/property_strings.hpp"
#include "text/hidden_class.hpp"
#include "text/page_text.hpp"
#include "text/rect.hpp"
#include "text/text_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;
using pdfkit::redact::MatchFinder;
using pdfkit::redact::PropertyStrings;

constexpr std::string_view shape =
    R"({"values": [{"text": "<non-empty text>", "folding": {"case", "diacritics", "whitespace", "hyphenation", "equivalents", "ignore"}}], "pages": [<page numbers from 1>], "context": <0 to 50>})";
constexpr std::int64_t default_context = 5;
constexpr std::int64_t most_context = 50;

[[noreturn]] void refuse(const std::string& failed, const std::string& fix) {
  throw Error(ErrorCode::invalid_argument, "pdfkit_find values_json " + failed, std::string(shape),
              fix);
}

std::string where(std::size_t index) {
  return "value " + std::to_string(index);
}

MatchFinder::Match read_value(const JsonValue& item, std::size_t index) {
  if (item.kind() != JsonValue::Kind::object) {
    refuse(where(index) + " is " + std::string(JsonValue::kind_name(item.kind())),
           R"(write each value as an object such as {"text": "Jane Doe"})");
  }
  MatchFinder::Match match;
  match.folding = MatchFinder::Folding::literal();
  bool text = false;
  for (const auto& [name, value] : item.as_object()) {
    if (name == "text") {
      if (value.kind() != JsonValue::Kind::string) {
        refuse(
            where(index) + " gives \"text\" as " + std::string(JsonValue::kind_name(value.kind())),
            "write \"text\" as a string");
      }
      text = true;
      match.search = value.as_string();
    } else if (name == "folding") {
      match.folding = pdfkit::api::FoldingArgument::read(
          value, "pdfkit_find values_json " + where(index), std::string(shape));
    } else {
      refuse(where(index) + " names the unknown key \"" + name + "\"", "use text and folding only");
    }
  }
  if (!text || match.search.empty()) {
    refuse(where(index) + (text ? " has an empty \"text\"" : " has no \"text\""),
           "give each value a non-empty text to find");
  }
  return match;
}

std::vector<std::uint32_t> read_pages(const JsonValue* pages, std::size_t page_count) {
  std::vector<std::uint32_t> out;
  if (pages == nullptr) {
    for (std::size_t i = 1; i <= page_count; ++i) {
      out.push_back(static_cast<std::uint32_t>(i));
    }
    return out;
  }
  if (pages->kind() != JsonValue::Kind::array) {
    refuse("gives \"pages\" as " + std::string(JsonValue::kind_name(pages->kind())),
           "list the pages as an array of page numbers, such as [1, 3]");
  }
  std::set<std::int64_t> seen;
  for (const JsonValue& item : pages->as_array()) {
    const std::optional<std::int64_t> number =
        item.kind() == JsonValue::Kind::number ? item.as_int64() : std::nullopt;
    if (!number || *number < 1 || std::cmp_greater(*number, page_count)) {
      throw Error(
          ErrorCode::invalid_argument,
          "pdfkit_find values_json asks for page " +
              (number ? std::to_string(*number) : std::string("that is not a whole number")) +
              " of a document of " + std::to_string(page_count) + " pages",
          "page numbers from 1 to " + std::to_string(page_count),
          "ask only for pages the document has; pdfkit_inspect gives the page count");
    }
    if (!seen.insert(*number).second) {
      throw Error(ErrorCode::invalid_argument,
                  "pdfkit_find values_json lists page " + std::to_string(*number) + " twice",
                  "each page at most once", "remove the repeated page number");
    }
    out.push_back(static_cast<std::uint32_t>(*number));
  }
  // The occurrences come in page order, whatever order the pages are asked in.
  std::ranges::sort(out);
  return out;
}

std::size_t read_context(const JsonValue* context) {
  if (context == nullptr) {
    return static_cast<std::size_t>(default_context);
  }
  const std::optional<std::int64_t> number =
      context->kind() == JsonValue::Kind::number ? context->as_int64() : std::nullopt;
  if (!number || *number < 0 || *number > most_context) {
    refuse("gives \"context\" as " + (number ? std::to_string(*number)
                                             : std::string(JsonValue::kind_name(context->kind()))),
           "give the words of context on each side as a whole number from 0 to 50");
  }
  return static_cast<std::size_t>(*number);
}

std::string fingerprint(const pdfkit::syntax::Document& document) {
  const auto digest = pdfkit::platform::crypto::Sha2::digest(
      pdfkit::platform::crypto::Sha2::Variant::sha256, document.bytes());
  constexpr std::string_view hex = "0123456789abcdef";
  std::string out;
  for (const std::byte value : digest) {
    const auto byte = std::to_integer<unsigned>(value);
    out += hex[byte >> 4U];
    out += hex[byte & 0x0FU];
  }
  return out;
}

// One occurrence as the answer writes it.
struct Found {
  MatchFinder::Located located;
  std::vector<pdfkit::text::Rect> rectangles;
  std::uint16_t hidden = 0;
  std::string before;
  std::string after;
  std::string glued_before;
  std::string glued_after;
};

void write_found(pdfkit::platform::JsonWriter& writer, const Found& found, std::size_t index) {
  const MatchFinder::Located& located = found.located;
  writer.begin_object();
  writer.key("occurrence").unsigned_integer(index);
  writer.key("page").unsigned_integer(located.page);
  writer.key("glyphs").begin_array();
  for (const auto& [first, last] : located.ranges) {
    writer.begin_object();
    writer.key("page").unsigned_integer(located.page);
    writer.key("first").unsigned_integer(first);
    writer.key("last").unsigned_integer(last);
    writer.end_object();
  }
  writer.end_array();
  writer.key("text").string(located.text);
  writer.key("order").string(MatchFinder::order_name(located.order));
  writer.key("rectangles").begin_array();
  for (const pdfkit::text::Rect& box : found.rectangles) {
    writer.begin_array();
    for (const double value : {box.left, box.bottom, box.right, box.top}) {
      writer.number(std::round(value * 100.0) / 100.0);
    }
    writer.end_array();
  }
  writer.end_array();
  writer.key("hidden").begin_array();
  for (const pdfkit::text::HiddenClass value : pdfkit::text::hidden_classes) {
    if ((found.hidden & pdfkit::text::hidden_bit(value)) != 0) {
      writer.string(pdfkit::text::hidden_class_name(value));
    }
  }
  writer.end_array();
  writer.key("before").string(found.before);
  writer.key("after").string(found.after);
  writer.key("glued_before").string(found.glued_before);
  writer.key("glued_after").string(found.glued_after);
  writer.end_object();
}

// One occurrence in a marked-content string.
struct InString {
  std::uint32_t page = 0;
  PropertyStrings::List list;
  PropertyStrings::String string;
  std::size_t first = 0;
  std::size_t last = 0;
  std::string text;
  std::string glued_before;
  std::string glued_after;
};

void write_in_string(pdfkit::platform::JsonWriter& writer, const InString& found,
                     std::size_t index) {
  writer.begin_object();
  writer.key("occurrence").unsigned_integer(index);
  writer.key("page").unsigned_integer(found.page);
  writer.key("list").unsigned_integer(found.list.index);
  writer.key("surface").string(PropertyStrings::surface_name(found.string.surface));
  writer.key("path").begin_array();
  for (const PropertyStrings::Step& step : found.string.path) {
    if (const std::string* key = std::get_if<std::string>(&step)) {
      writer.string(*key);
    } else {
      writer.unsigned_integer(std::get<std::size_t>(step));
    }
  }
  writer.end_array();
  writer.key("stream").unsigned_integer(found.list.stream);
  writer.key("operator").unsigned_integer(found.list.operation);
  writer.key("properties");
  if (found.list.name) {
    writer.string(*found.list.name);
  } else {
    writer.null();
  }
  writer.key("object");
  if (found.list.identity.object != 0) {
    writer.unsigned_integer(found.list.identity.object);
  } else {
    writer.null();
  }
  writer.key("string").string(found.string.text);
  writer.key("first").unsigned_integer(found.first);
  writer.key("last").unsigned_integer(found.last);
  writer.key("text").string(found.text);
  writer.key("glued_before").string(found.glued_before);
  writer.key("glued_after").string(found.glued_after);
  writer.end_object();
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_find(pdfkit_document* doc, const char* values_json,
                                                pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_find", "out");
    pdfkit_document& handle = pdfkit::api::require_document(doc, "pdfkit_find");
    const JsonValue input =
        pdfkit::api::parse_json_object(values_json, "pdfkit_find", "values_json", true);
    for (const auto& [name, value] : input.as_object()) {
      if (name != "values" && name != "pages" && name != "context") {
        refuse("names the unknown key \"" + name + "\"", "give only values, pages and context");
      }
    }
    const JsonValue* list = input.find("values");
    if (list == nullptr || list->kind() != JsonValue::Kind::array) {
      refuse("has no \"values\" array", R"(list the values as {"values": [{"text": "..."}]})");
    }
    std::vector<MatchFinder::Match> values;
    values.reserve(list->as_array().size());
    for (std::size_t i = 0; i < list->as_array().size(); ++i) {
      values.push_back(read_value(list->as_array()[i], i));
    }
    pdfkit::syntax::Document& document = *handle.document;
    const std::vector<std::uint32_t> pages =
        read_pages(input.find("pages"), document.pages().size());
    const std::size_t context = read_context(input.find("context"));

    // Per value, its occurrences page by page, each page's in the order of
    // their first glyph.
    std::vector<std::vector<Found>> found(values.size());
    std::vector<std::vector<InString>> in_strings(values.size());
    pdfkit::text::TextReader reader(document);
    for (const std::uint32_t number : pages) {
      const pdfkit::text::PageText page = reader.read(number);
      MatchFinder finder(page);
      const std::vector<PropertyStrings::List> lists =
          PropertyStrings::lists(document, page.content);
      const std::vector<PropertyStrings::String> strings =
          PropertyStrings::strings(document, lists);
      for (const PropertyStrings::String& string : strings) {
        const std::u32string decoded = pdfkit::platform::unicode::Folding::decode(string.text);
        for (std::size_t v = 0; v < values.size(); ++v) {
          for (const auto& [first, last] : MatchFinder::ranges_in(decoded, values[v])) {
            InString entry;
            entry.page = number;
            entry.list = lists[string.list];
            entry.string = string;
            entry.first = first;
            entry.last = last;
            entry.text = pdfkit::platform::unicode::Folding::encode(
                std::u32string_view(decoded).substr(first, last - first));
            std::tie(entry.glued_before, entry.glued_after) =
                PropertyStrings::glued(decoded, first, last);
            in_strings[v].push_back(std::move(entry));
          }
        }
      }
      for (std::size_t v = 0; v < values.size(); ++v) {
        std::vector<MatchFinder::Located> located = finder.locate(values[v]);
        std::ranges::stable_sort(located, {}, [](const MatchFinder::Located& item) {
          return item.ranges.empty() ? std::size_t{0} : item.ranges.front().first;
        });
        for (MatchFinder::Located& item : located) {
          Found entry;
          entry.rectangles = finder.rectangles(item);
          entry.hidden = finder.hidden(item);
          std::tie(entry.before, entry.after) = finder.context(item, context);
          std::tie(entry.glued_before, entry.glued_after) = finder.glued(item);
          entry.located = std::move(item);
          found[v].push_back(std::move(entry));
        }
      }
    }

    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("fingerprint").string(fingerprint(document));
    writer.key("values").begin_array();
    for (std::size_t v = 0; v < values.size(); ++v) {
      writer.begin_object();
      writer.key("value").unsigned_integer(v);
      writer.key("text").string(values[v].search);
      writer.key("count").unsigned_integer(found[v].size());
      writer.key("occurrences").begin_array();
      for (std::size_t i = 0; i < found[v].size(); ++i) {
        write_found(writer, found[v][i], i);
      }
      writer.end_array();
      writer.key("strings").begin_array();
      for (std::size_t i = 0; i < in_strings[v].size(); ++i) {
        write_in_string(writer, in_strings[v][i], i);
      }
      writer.end_array();
      writer.end_object();
    }
    writer.end_array();
    writer.end_object();
    pdfkit::api::write_buffer(out, writer.take());
    return PDFKIT_OK;
  });
}
