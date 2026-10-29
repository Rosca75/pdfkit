// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 5, Redact text: pdfkit_redact_text adds text matches to
// an edit plan, as glyph ranges from an earlier pdfkit_text (with the
// fingerprint it reported) or as search strings with the caller's folding,
// each with its placeholder, and answers per match the occurrences found,
// their device rectangles and how each placeholder is fitted (CLAUDE.md
// section 2, the pdfkit_redact_text section of docs/ABI.md). A glyph range
// is one range or an array of ranges on one page, read as one expression at
// one place. An occurrence names one occurrence of a text on a page by its
// index in pdfkit_find's order, a rectangle it meets, or the words before
// or after it, and is resolved to the glyph ranges pdfkit_find reports for
// it, then read as a glyph range. A string match names one range of one
// string of a marked-content property list as pdfkit_find reports it (the
// page, the list, the path and the range, with any other key of the entry
// checked against the document). Every match is checked before any is
// recorded, so a refused call leaves the plan as it was. The text is
// removed when the plan is written.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/folding_argument.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "redact/match_finder.hpp"
#include "redact/placeholder_fitting.hpp"
#include "redact/property_strings.hpp"
#include "redact/text_plan.hpp"
#include "redact/text_redactor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;
using pdfkit::redact::MatchFinder;
using pdfkit::redact::TextPlan;

constexpr std::string_view shape =
    R"({"matches": [...]}, each match either {"search": "<text>", "folding": {"case", "diacritics", "whitespace", "hyphenation", "equivalents", "ignore"}, "placeholder": "<text>"} or {"glyphs": {"page": <n>, "first": <n>, "last": <n>} or an array of such ranges on one page, "fingerprint": "<from pdfkit_text>", "placeholder": "<text>"} or {"occurrence": {"text": "<text>", "page": <n>, "folding": {...}, and one or more of "index": <n>, "near": [left, bottom, right, top], "before": "<text>", "after": "<text>"}, "placeholder": "<text>"} or {"string": {"page": <n>, "list": <n>, "path": [<keys and indices>], "first": <n>, "last": <n>, and any other key of the pdfkit_find entry}, "fingerprint": "<from pdfkit_find>", "placeholder": "<text>"})";
constexpr std::string_view default_placeholder = "[REDACTED]";

[[noreturn]] void refuse(const std::string& failed, const std::string& fix) {
  throw Error(ErrorCode::invalid_argument, "pdfkit_redact_text matches_json " + failed,
              std::string(shape), fix);
}

std::string where(std::size_t index) {
  return "match " + std::to_string(index);
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

// One {"page", "first", "last"} object.
TextPlan::Range read_range(const JsonValue& value, std::size_t index) {
  if (value.kind() != JsonValue::Kind::object) {
    refuse(
        where(index) + " gives a glyph range as " + std::string(JsonValue::kind_name(value.kind())),
        R"(write the range as {"page": 1, "first": 0, "last": 5}, or an array of such ranges)");
  }
  for (const auto& [key, unused] : value.as_object()) {
    if (key != "page" && key != "first" && key != "last") {
      refuse(where(index) + " names \"" + key + R"(" in "glyphs")",
             "give only page, first and last");
    }
  }
  TextPlan::Range range;
  const std::size_t page = count_member(value, index, "page");
  range.page = page > 0xFFFFFFFFU ? 0U : static_cast<std::uint32_t>(page);
  range.first = count_member(value, index, "first");
  range.last = count_member(value, index, "last");
  return range;
}

MatchFinder::Folding read_folding(const JsonValue& value, std::size_t index) {
  return pdfkit::api::FoldingArgument::read(
      value, "pdfkit_redact_text matches_json " + where(index), std::string(shape));
}

// The "occurrence" object of a match.
TextPlan::OccurrenceQuery read_occurrence(const JsonValue& value, std::size_t index) {
  if (value.kind() != JsonValue::Kind::object) {
    refuse(where(index) + " gives \"occurrence\" as " +
               std::string(JsonValue::kind_name(value.kind())),
           R"(write the occurrence as {"text": "EU", "page": 3, "index": 0})");
  }
  TextPlan::OccurrenceQuery query;
  query.folding = MatchFinder::Folding::literal();
  bool text = false;
  bool page = false;
  for (const auto& [key, item] : value.as_object()) {
    if (key == "text") {
      text = true;
      query.text = text_member(item, index, key);
    } else if (key == "page" || key == "index") {
      const std::optional<std::int64_t> number =
          item.kind() == JsonValue::Kind::number ? item.as_int64() : std::nullopt;
      if (!number || *number < 0) {
        refuse(where(index) + " gives the occurrence's \"" + key + "\" as no whole number",
               R"(give "page" from 1 and "index" from 0 as whole numbers)");
      }
      if (key == "page") {
        page = true;
        query.page =
            std::cmp_greater(*number, 0xFFFFFFFFU) ? 0U : static_cast<std::uint32_t>(*number);
      } else {
        query.index = static_cast<std::size_t>(*number);
      }
    } else if (key == "folding") {
      query.folding = read_folding(item, index);
    } else if (key == "near") {
      const bool four = item.kind() == JsonValue::Kind::array && item.as_array().size() == 4 &&
                        std::ranges::all_of(item.as_array(), [](const JsonValue& v) {
                          return v.kind() == JsonValue::Kind::number;
                        });
      const JsonValue::Array* box = four ? &item.as_array() : nullptr;
      if (box == nullptr || (*box)[0].as_number() > (*box)[2].as_number() ||
          (*box)[1].as_number() > (*box)[3].as_number()) {
        refuse(where(index) + " gives the occurrence's \"near\" as no rectangle",
               "give near as [left, bottom, right, top] in points, left not past right and "
               "bottom not above top");
      }
      query.near = pdfkit::text::Rect::of((*box)[0].as_number(), (*box)[1].as_number(),
                                          (*box)[2].as_number(), (*box)[3].as_number());
    } else if (key == "before") {
      query.before = text_member(item, index, key);
    } else if (key == "after") {
      query.after = text_member(item, index, key);
    } else {
      refuse(where(index) + " names \"" + key + R"(" in "occurrence")",
             "give only text, page, folding, index, near, before and after");
    }
  }
  if (!text || query.text.empty() || !page) {
    refuse(where(index) + R"( gives an occurrence without a non-empty "text" and a "page")",
           "give the occurrence's text and its page");
  }
  if (!query.index && !query.near && !query.before && !query.after) {
    refuse(where(index) + " gives an occurrence with no narrowing",
           R"(give at least one of "index", "near", "before" and "after"; a search match )"
           "removes every occurrence");
  }
  return query;
}

// A whole number at least 0 of the "string" object.
std::size_t whole_member(const JsonValue& value, std::size_t index, const std::string& key) {
  const std::optional<std::int64_t> number =
      value.kind() == JsonValue::Kind::number ? value.as_int64() : std::nullopt;
  if (!number || *number < 0) {
    refuse(where(index) + " gives the string's \"" + key + "\" as no whole number",
           R"(copy "page", "list", "first", "last" and the other numbers of the pdfkit_find )"
           "entry unchanged");
  }
  return static_cast<std::size_t>(*number);
}

// The "string" object of a match: a pdfkit_find string entry.
TextPlan::StringTarget read_string(const JsonValue& value, std::size_t index) {
  if (value.kind() != JsonValue::Kind::object) {
    refuse(where(index) + " gives \"string\" as " + std::string(JsonValue::kind_name(value.kind())),
           R"(give the string as {"page": 1, "list": 0, "path": ["Alt"], "first": 9, "last": 14})");
  }
  TextPlan::StringTarget target;
  bool page = false;
  bool list = false;
  bool path = false;
  bool first = false;
  bool last = false;
  const auto narrow = [](std::size_t number) {
    return number > 0xFFFFFFFFU ? 0xFFFFFFFFU : static_cast<std::uint32_t>(number);
  };
  for (const auto& [key, item] : value.as_object()) {
    if (key == "page") {
      page = true;
      target.page = narrow(whole_member(item, index, key));
    } else if (key == "list") {
      list = true;
      target.list = whole_member(item, index, key);
    } else if (key == "first") {
      first = true;
      target.first = whole_member(item, index, key);
    } else if (key == "last") {
      last = true;
      target.last = whole_member(item, index, key);
    } else if (key == "occurrence") {
      // Its index among the value's strings: it names nothing in the
      // document, so it is read and not checked.
      (void)whole_member(item, index, key);
    } else if (key == "stream") {
      target.stream = narrow(whole_member(item, index, key));
    } else if (key == "operator") {
      target.operation = whole_member(item, index, key);
    } else if (key == "object") {
      target.object =
          item.kind() == JsonValue::Kind::null ? 0U : narrow(whole_member(item, index, key));
    } else if (key == "properties") {
      target.properties = item.kind() == JsonValue::Kind::null
                              ? std::optional<std::string>()
                              : std::optional<std::string>(text_member(item, index, key));
    } else if (key == "surface") {
      target.surface = text_member(item, index, key);
    } else if (key == "string") {
      target.string = text_member(item, index, key);
    } else if (key == "text") {
      target.text = text_member(item, index, key);
    } else if (key == "glued_before") {
      target.glued_before = text_member(item, index, key);
    } else if (key == "glued_after") {
      target.glued_after = text_member(item, index, key);
    } else if (key == "path") {
      path = true;
      const bool steps =
          item.kind() == JsonValue::Kind::array && !item.as_array().empty() &&
          item.as_array().front().kind() == JsonValue::Kind::string &&
          std::ranges::all_of(item.as_array(), [](const JsonValue& step) {
            return step.kind() == JsonValue::Kind::string ||
                   (step.kind() == JsonValue::Kind::number && step.as_int64().value_or(-1) >= 0);
          });
      if (!steps) {
        refuse(where(index) + " gives the string's \"path\" as no path",
               R"(give the path as pdfkit_find reports it: keys and array indices from the list, )"
               R"(a key first, such as ["Alt"])");
      }
      for (const JsonValue& step : item.as_array()) {
        if (step.kind() == JsonValue::Kind::string) {
          target.path.emplace_back(step.as_string());
        } else {
          target.path.emplace_back(static_cast<std::size_t>(*step.as_int64()));
        }
      }
    } else {
      refuse(where(index) + " names \"" + key + R"(" in "string")",
             "give only the keys of a pdfkit_find string entry");
    }
  }
  if (!page || !list || !path || !first || !last) {
    refuse(where(index) + R"( gives a string without "page", "list", "path", "first" and "last")",
           "copy the string entry of pdfkit_find unchanged");
  }
  return target;
}

// A match, the ranges of a glyph range given as an array (empty for one
// range or a search), the occurrence it names when it is one, and the
// string it names when it is a string match.
MatchFinder::Match read_match(const JsonValue& item, std::size_t index,
                              std::vector<TextPlan::Range>& ranges,
                              std::optional<TextPlan::OccurrenceQuery>& query,
                              std::optional<TextPlan::StringTarget>& target) {
  if (item.kind() != JsonValue::Kind::object) {
    refuse(where(index) + " is " + std::string(JsonValue::kind_name(item.kind())),
           "write each match as an object");
  }
  MatchFinder::Match match;
  match.folding = MatchFinder::Folding::literal();
  match.placeholder = std::string(default_placeholder);
  bool search = false;
  bool glyphs = false;
  bool occurrence = false;
  bool string = false;
  for (const auto& [name, value] : item.as_object()) {
    if (name == "search") {
      search = true;
      match.search = text_member(value, index, name);
    } else if (name == "placeholder") {
      match.placeholder = text_member(value, index, name);
    } else if (name == "fingerprint") {
      match.fingerprint = text_member(value, index, name);
    } else if (name == "folding") {
      match.folding = read_folding(value, index);
    } else if (name == "occurrence") {
      occurrence = true;
      match.kind = MatchFinder::Match::Kind::glyphs;
      query = read_occurrence(value, index);
    } else if (name == "string") {
      string = true;
      match.kind = MatchFinder::Match::Kind::string;
      target = read_string(value, index);
      match.page = target->page;
    } else if (name == "glyphs") {
      glyphs = true;
      match.kind = MatchFinder::Match::Kind::glyphs;
      if (value.kind() == JsonValue::Kind::array) {
        for (const JsonValue& entry : value.as_array()) {
          ranges.push_back(read_range(entry, index));
        }
        // Checked whole here (one page, none empty, none overlapping), so an
        // empty array is refused rather than read as one range.
        ranges = TextPlan::located_ranges(std::move(ranges), where(index));
        match.page = ranges.front().page;
        match.first = ranges.front().first;
        match.last = ranges.front().last;
      } else {
        const TextPlan::Range range = read_range(value, index);
        match.page = range.page;
        match.first = range.first;
        match.last = range.last;
      }
    } else {
      refuse(where(index) + " names the unknown key \"" + name + "\"",
             "use search, folding, glyphs, occurrence, string, fingerprint and placeholder only");
    }
  }
  const int forms = (search ? 1 : 0) + (glyphs ? 1 : 0) + (occurrence ? 1 : 0) + (string ? 1 : 0);
  if (forms != 1) {
    refuse(
        where(index) +
            (forms == 0 ? R"( has none of "search", "glyphs", "occurrence" and "string")"
                        : R"( has more than one of "search", "glyphs", "occurrence" and "string")"),
        "give a match one of a search string, a glyph range, an occurrence or a string");
  }
  if (occurrence && item.find("folding") != nullptr) {
    refuse(where(index) + R"( gives "folding" beside "occurrence")",
           R"(give the occurrence's folding inside "occurrence")");
  }
  if (string && item.find("folding") != nullptr) {
    refuse(where(index) + R"( gives "folding" beside "string")",
           "give a string match without a folding: it names one range of one string");
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

// A string match's occurrence: its place and the text it replaces.
void write_string_found(pdfkit::platform::JsonWriter& writer,
                        const pdfkit::redact::TextRedactor::Found& found) {
  writer.begin_object();
  writer.key("page").unsigned_integer(found.page);
  writer.key("list").unsigned_integer(found.list);
  writer.key("path").begin_array();
  for (const pdfkit::redact::PropertyStrings::Step& step : found.path) {
    if (const std::string* key = std::get_if<std::string>(&step)) {
      writer.string(*key);
    } else {
      writer.unsigned_integer(std::get<std::size_t>(step));
    }
  }
  writer.end_array();
  writer.key("first").unsigned_integer(found.first);
  writer.key("last").unsigned_integer(found.last);
  writer.key("text").string(found.text);
  writer.end_object();
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
    std::vector<std::vector<TextPlan::Range>> ranges(list->as_array().size());
    matches.reserve(list->as_array().size());
    std::vector<std::optional<TextPlan::OccurrenceQuery>> queries(list->as_array().size());
    std::vector<std::optional<TextPlan::StringTarget>> targets(list->as_array().size());
    for (std::size_t i = 0; i < list->as_array().size(); ++i) {
      matches.push_back(read_match(list->as_array()[i], i, ranges[i], queries[i], targets[i]));
    }
    // Each occurrence resolved to its glyph ranges on this document, then
    // read as a glyph range.
    for (std::size_t i = 0; i < queries.size(); ++i) {
      if (queries[i]) {
        ranges[i] = TextPlan::resolve(handle.plan->document(), *queries[i], where(i));
        if (matches[i].fingerprint.empty()) {
          matches[i].fingerprint = handle.plan->fingerprint();
        }
      }
    }
    const std::vector<MatchFinder::Match> given = matches;
    const std::vector<std::vector<TextPlan::Range>> arrays = ranges;
    const std::vector<pdfkit::redact::TextRedactor::MatchReport> reports =
        pdfkit::redact::TextRedactor::add(*handle.plan, std::move(matches), std::move(ranges),
                                          std::move(targets));

    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("fingerprint").string(handle.plan->fingerprint());
    writer.key("matches").begin_array();
    for (std::size_t i = 0; i < reports.size(); ++i) {
      const MatchFinder::Match& match = given[i];
      writer.begin_object();
      writer.key("match").unsigned_integer(i);
      const bool search = match.kind == MatchFinder::Match::Kind::search;
      const bool string = match.kind == MatchFinder::Match::Kind::string;
      writer.key("kind").string(search       ? "search"
                                : string     ? "string"
                                : queries[i] ? "occurrence"
                                             : "glyphs");
      if (search) {
        writer.key("search").string(match.search);
      }
      writer.key("placeholder").string(match.placeholder);
      writer.key("count").unsigned_integer(reports[i].occurrences.size());
      writer.key("occurrences").begin_array();
      for (const pdfkit::redact::TextRedactor::Found& found : reports[i].occurrences) {
        if (string) {
          write_string_found(writer, found);
          continue;
        }
        writer.begin_object();
        writer.key("page").unsigned_integer(found.page);
        // A glyph range given as an array is answered with its ranges.
        writer.key("glyphs").begin_array();
        if (arrays[i].empty()) {
          writer.unsigned_integer(found.first).unsigned_integer(found.last);
        } else {
          for (const TextPlan::Range& range : found.ranges) {
            writer.begin_array()
                .unsigned_integer(range.first)
                .unsigned_integer(range.last)
                .end_array();
          }
        }
        writer.end_array();
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
