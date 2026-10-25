// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 5, Redact text: pdfkit_redact_text adds text matches to
// an edit plan, as glyph ranges from an earlier pdfkit_text (with the
// fingerprint it reported) or as search strings with the caller's folding,
// each with its placeholder, and answers per match the occurrences found,
// their device rectangles and how each placeholder is fitted (CLAUDE.md
// section 2, the pdfkit_redact_text section of docs/ABI.md). Every match is
// checked before any is recorded, so a refused call leaves the plan as it
// was. The text is removed when the plan is written.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "redact/match_finder.hpp"
#include "redact/placeholder_fitting.hpp"
#include "redact/text_redactor.hpp"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;
using pdfkit::redact::MatchFinder;

constexpr std::string_view shape =
    R"({"matches": [...]}, each match either {"search": "<text>", "folding": {"case", "diacritics", "whitespace", "hyphenation"}, "placeholder": "<text>"} or {"glyphs": {"page": <n>, "first": <n>, "last": <n>}, "fingerprint": "<from pdfkit_text>", "placeholder": "<text>"})";
constexpr std::string_view default_placeholder = "[REDACTED]";

[[noreturn]] void refuse(const std::string& failed, const std::string& fix) {
  throw Error(ErrorCode::invalid_argument, "pdfkit_redact_text matches_json " + failed,
              std::string(shape), fix);
}

std::string where(std::size_t index) {
  return "match " + std::to_string(index);
}

bool flag(const JsonValue& value, std::size_t index, const std::string& name) {
  if (value.kind() != JsonValue::Kind::boolean) {
    refuse(where(index) + " gives the folding \"" + name + "\" as " +
               std::string(JsonValue::kind_name(value.kind())),
           "write \"" + name + "\": true or false");
  }
  return value.as_bool();
}

const std::string& text_member(const JsonValue& value, std::size_t index, const std::string& name) {
  if (value.kind() != JsonValue::Kind::string) {
    refuse(where(index) + " gives \"" + name + "\" as " +
               std::string(JsonValue::kind_name(value.kind())),
           "write \"" + name + "\" as a string");
  }
  return value.as_string();
}

std::size_t count_member(const JsonValue& glyphs, std::size_t index, const std::string& name) {
  const JsonValue* value = glyphs.find(name);
  const std::optional<std::int64_t> number =
      value != nullptr && value->kind() == JsonValue::Kind::number ? value->as_int64()
                                                                   : std::nullopt;
  if (!number || *number < 0) {
    refuse(where(index) + " has no whole number \"" + name + R"(" in "glyphs")",
           R"(give "page" (from 1), "first" and "last" (positions in the page's glyph list )"
           "of pdfkit_text) as whole numbers");
  }
  return static_cast<std::size_t>(*number);
}

MatchFinder::Match read_match(const JsonValue& item, std::size_t index) {
  if (item.kind() != JsonValue::Kind::object) {
    refuse(where(index) + " is " + std::string(JsonValue::kind_name(item.kind())),
           "write each match as an object");
  }
  MatchFinder::Match match;
  match.placeholder = std::string(default_placeholder);
  bool search = false;
  bool glyphs = false;
  for (const auto& [name, value] : item.as_object()) {
    if (name == "search") {
      search = true;
      match.search = text_member(value, index, name);
    } else if (name == "placeholder") {
      match.placeholder = text_member(value, index, name);
    } else if (name == "fingerprint") {
      match.fingerprint = text_member(value, index, name);
    } else if (name == "folding") {
      if (value.kind() != JsonValue::Kind::object) {
        refuse(where(index) + " gives \"folding\" as " +
                   std::string(JsonValue::kind_name(value.kind())),
               "write the folding as an object of true or false flags");
      }
      for (const auto& [key, setting] : value.as_object()) {
        if (key == "case") {
          match.folding.text.case_fold = flag(setting, index, key);
        } else if (key == "diacritics") {
          match.folding.text.diacritics = flag(setting, index, key);
        } else if (key == "whitespace") {
          match.folding.text.whitespace = flag(setting, index, key);
        } else if (key == "hyphenation") {
          match.folding.hyphenation = flag(setting, index, key);
        } else {
          refuse(where(index) + " names the unknown folding \"" + key + "\"",
                 "use only case, diacritics, whitespace and hyphenation");
        }
      }
    } else if (name == "glyphs") {
      glyphs = true;
      if (value.kind() != JsonValue::Kind::object) {
        refuse(where(index) + " gives \"glyphs\" as " +
                   std::string(JsonValue::kind_name(value.kind())),
               R"(write the range as {"page": 1, "first": 0, "last": 5})");
      }
      for (const auto& [key, unused] : value.as_object()) {
        if (key != "page" && key != "first" && key != "last") {
          refuse(where(index) + " names \"" + key + R"(" in "glyphs")",
                 "give only page, first and last");
        }
      }
      match.kind = MatchFinder::Match::Kind::glyphs;
      const std::size_t page = count_member(value, index, "page");
      match.page = page > 0xFFFFFFFFU ? 0U : static_cast<std::uint32_t>(page);
      match.first = count_member(value, index, "first");
      match.last = count_member(value, index, "last");
    } else {
      refuse(where(index) + " names the unknown key \"" + name + "\"",
             "use search, folding, glyphs, fingerprint and placeholder only");
    }
  }
  if (search == glyphs) {
    refuse(where(index) + (search ? R"( has both "search" and "glyphs")"
                                  : R"( has neither "search" nor "glyphs")"),
           "give a match either a search string or a glyph range");
  }
  return match;
}

void write_box(pdfkit::platform::JsonWriter& writer, const pdfkit::text::Rect& box) {
  writer.begin_array();
  for (const double value : {box.left, box.bottom, box.right, box.top}) {
    writer.number(std::round(value * 100.0) / 100.0);
  }
  writer.end_array();
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_redact_text(pdfkit_edit* edit, const char* matches_json,
                                                       pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_redact_text", "out");
    pdfkit_edit& handle = pdfkit::api::require_edit(edit, "pdfkit_redact_text");
    const JsonValue input =
        pdfkit::api::parse_json_object(matches_json, "pdfkit_redact_text", "matches_json", true);
    for (const auto& [name, value] : input.as_object()) {
      if (name != "matches") {
        refuse("names the unknown key \"" + name + "\"", "give only \"matches\"");
      }
    }
    const JsonValue* list = input.find("matches");
    if (list == nullptr || list->kind() != JsonValue::Kind::array) {
      refuse("has no \"matches\" array", "list the matches as {\"matches\": [...]}");
    }
    std::vector<MatchFinder::Match> matches;
    matches.reserve(list->as_array().size());
    for (std::size_t i = 0; i < list->as_array().size(); ++i) {
      matches.push_back(read_match(list->as_array()[i], i));
    }
    const std::vector<MatchFinder::Match> given = matches;
    const std::vector<pdfkit::redact::TextRedactor::MatchReport> reports =
        pdfkit::redact::TextRedactor::add(*handle.plan, std::move(matches));

    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("fingerprint").string(handle.plan->fingerprint());
    writer.key("matches").begin_array();
    for (std::size_t i = 0; i < reports.size(); ++i) {
      const MatchFinder::Match& match = given[i];
      writer.begin_object();
      writer.key("match").unsigned_integer(i);
      const bool search = match.kind == MatchFinder::Match::Kind::search;
      writer.key("kind").string(search ? "search" : "glyphs");
      if (search) {
        writer.key("search").string(match.search);
      }
      writer.key("placeholder").string(match.placeholder);
      writer.key("count").unsigned_integer(reports[i].occurrences.size());
      writer.key("occurrences").begin_array();
      for (const pdfkit::redact::TextRedactor::Found& found : reports[i].occurrences) {
        writer.begin_object();
        writer.key("page").unsigned_integer(found.page);
        writer.key("glyphs")
            .begin_array()
            .unsigned_integer(found.first)
            .unsigned_integer(found.last)
            .end_array();
        writer.key("text").string(found.text);
        writer.key("rectangles").begin_array();
        for (const pdfkit::text::Rect& box : found.rectangles) {
          write_box(writer, box);
        }
        writer.end_array();
        writer.key("placeholder_drawn").boolean(found.drawn);
        writer.key("fitting");
        if (found.drawn) {
          writer.string(pdfkit::redact::PlaceholderFitting::name(found.fitting));
        } else {
          writer.null();
        }
        writer.key("scale").number(std::round(found.scale * 1000.0) / 1000.0);
        writer.key("codes").begin_array();
        for (const std::string& codes : found.codes) {
          writer.string(codes);
        }
        writer.end_array();
        writer.end_object();
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
