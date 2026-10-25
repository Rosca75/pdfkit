// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Catalogue function 10.2, Check: pdfkit_check searches any PDF bytes for
// surviving occurrences of strings under their folding rules, with the same
// search the self-check of pdfkit_write runs (docs/DESIGN.md section 18),
// and answers every occurrence by page and surface (CLAUDE.md section 2, the
// pdfkit_check section of docs/ABI.md). Finding a string is the answer, not a
// failure; the call fails only when the bytes do not open, with the open's
// refusal, under the default open limits.

#include "pdfkit/pdfkit.h"

#include "api/arguments.hpp"
#include "api/guarded.hpp"
#include "platform/byte_span.hpp"
#include "platform/error.hpp"
#include "platform/export.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "redact/self_check.hpp"
#include "syntax/limits.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonValue;
using pdfkit::redact::MatchFinder;

constexpr std::string_view shape =
    R"({"strings": [{"text": "<text>", "folding": {"case", "diacritics", "whitespace", "hyphenation"}}]})";

[[noreturn]] void refuse(const std::string& failed, const std::string& fix) {
  throw Error(ErrorCode::invalid_argument, "pdfkit_check strings_json " + failed,
              std::string(shape), fix);
}

MatchFinder::Match read_string(const JsonValue& item, std::size_t index) {
  const std::string where = "string " + std::to_string(index);
  if (item.kind() != JsonValue::Kind::object) {
    refuse(where + " is " + std::string(JsonValue::kind_name(item.kind())),
           R"(write each string as {"text": "..."})");
  }
  MatchFinder::Match match;
  bool text = false;
  for (const auto& [name, value] : item.as_object()) {
    if (name == "text" && value.kind() == JsonValue::Kind::string) {
      match.search = value.as_string();
      text = true;
    } else if (name == "folding" && value.kind() == JsonValue::Kind::object) {
      for (const auto& [key, setting] : value.as_object()) {
        if (setting.kind() != JsonValue::Kind::boolean) {
          std::string failed = where;
          failed += " gives the folding \"" + key + "\" as ";
          failed += JsonValue::kind_name(setting.kind());
          refuse(failed, "write each folding flag as true or false");
        }
        if (key == "case") {
          match.folding.text.case_fold = setting.as_bool();
        } else if (key == "diacritics") {
          match.folding.text.diacritics = setting.as_bool();
        } else if (key == "whitespace") {
          match.folding.text.whitespace = setting.as_bool();
        } else if (key == "hyphenation") {
          match.folding.hyphenation = setting.as_bool();
        } else {
          std::string failed = where;
          failed += " names the unknown folding \"" + key + "\"";
          refuse(failed, "use only case, diacritics, whitespace and hyphenation");
        }
      }
    } else {
      std::string failed = where;
      failed += " gives \"" + name + "\" as ";
      failed += JsonValue::kind_name(value.kind());
      refuse(failed, R"(give "text" as a string and "folding" as an object, nothing else)");
    }
  }
  if (!text) {
    refuse(where + " has no \"text\"", "give the string to search for as \"text\"");
  }
  return match;
}

}  // namespace

extern "C" PDFKIT_API pdfkit_status pdfkit_check(const std::uint8_t* bytes, std::size_t size,
                                                 const char* strings_json, pdfkit_buffer* out) {
  return pdfkit::api::guarded(out, [&]() -> pdfkit_status {
    pdfkit::api::require_out(out, "pdfkit_check", "out");
    pdfkit::api::require_bytes(bytes, size, "pdfkit_check");
    const JsonValue input =
        pdfkit::api::parse_json_object(strings_json, "pdfkit_check", "strings_json", true);
    for (const auto& [name, value] : input.as_object()) {
      if (name != "strings") {
        refuse("names the unknown key \"" + name + "\"", "give only \"strings\"");
      }
    }
    const JsonValue* list = input.find("strings");
    if (list == nullptr || list->kind() != JsonValue::Kind::array) {
      refuse("has no \"strings\" array", "list the strings as {\"strings\": [...]}");
    }
    std::vector<MatchFinder::Match> needles;
    needles.reserve(list->as_array().size());
    for (std::size_t i = 0; i < list->as_array().size(); ++i) {
      needles.push_back(read_string(list->as_array()[i], i));
    }
    const pdfkit::platform::ByteSpan data(reinterpret_cast<const std::byte*>(bytes), size);
    const pdfkit::redact::SelfCheck::Result result =
        pdfkit::redact::SelfCheck::run(data, pdfkit::syntax::Limits{}, needles, {});

    pdfkit::platform::JsonWriter writer;
    writer.begin_object();
    writer.key("strings").unsigned_integer(result.strings);
    writer.key("pages").unsigned_integer(result.pages);
    writer.key("count").unsigned_integer(result.survivors.size());
    writer.key("survivors").begin_array();
    for (const pdfkit::redact::SelfCheck::Survivor& survivor : result.survivors) {
      writer.begin_object();
      writer.key("string").string(survivor.string);
      writer.key("page");
      if (survivor.page) {
        writer.unsigned_integer(*survivor.page);
      } else {
        writer.null();
      }
      writer.key("surface").string(survivor.surface);
      writer.key("object");
      if (survivor.object != 0) {
        writer.unsigned_integer(survivor.object);
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
