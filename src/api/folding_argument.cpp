// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The folding object read key by key. A class maps each member to the
// class's first member, and a character may be named once in all the
// classes and the ignored list together, because a character in two places
// would match two different texts.

#include "api/folding_argument.hpp"

#include "platform/error.hpp"
#include "platform/unicode/folding.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

namespace pdfkit::api {

namespace {

using platform::Error;
using platform::ErrorCode;
using platform::JsonValue;
using TextFolding = platform::unicode::Folding;

constexpr std::string_view keys =
    "case, diacritics, whitespace and hyphenation (true or false), equivalents (an array of "
    "classes, each an array of one-character strings) and ignore (an array of one-character "
    "strings)";

std::string kind_of(const JsonValue& value) {
  return std::string(JsonValue::kind_name(value.kind()));
}

// The one code point of a member of a class or of the ignored list.
char32_t character(const JsonValue& value, const std::string& where, const std::string& expected,
                   const std::string& what) {
  if (value.kind() != JsonValue::Kind::string) {
    throw Error(ErrorCode::invalid_argument, where + " gives " + what + " as " + kind_of(value),
                expected, "write each character as a string of one character, such as \"'\"");
  }
  const std::u32string decoded = TextFolding::decode(value.as_string());
  if (decoded.size() != 1) {
    throw Error(ErrorCode::invalid_argument,
                where + " gives " + what + " as \"" + value.as_string() + "\", " +
                    std::to_string(decoded.size()) + " characters",
                expected,
                "write each character as a string of exactly one character (one code point)");
  }
  return decoded.front();
}

}  // namespace

redact::MatchFinder::Folding FoldingArgument::read(const JsonValue& value, const std::string& where,
                                                   const std::string& expected) {
  if (value.kind() != JsonValue::Kind::object) {
    throw Error(ErrorCode::invalid_argument, where + " gives \"folding\" as " + kind_of(value),
                expected, "write the folding as an object of " + std::string(keys));
  }
  redact::MatchFinder::Folding folding = redact::MatchFinder::Folding::literal();
  std::set<char32_t> named;
  const auto once = [&](char32_t code_point, const std::string& what) {
    if (!named.insert(code_point).second) {
      throw Error(ErrorCode::invalid_argument,
                  where + " names the character \"" + TextFolding::encode({&code_point, 1}) +
                      "\" twice in its folding (" + what + ")",
                  expected,
                  "name each character in one class or in ignore, once; merge two classes that "
                  "share a character");
    }
  };
  for (const auto& [key, setting] : value.as_object()) {
    if (key == "case" || key == "diacritics" || key == "whitespace" || key == "hyphenation") {
      if (setting.kind() != JsonValue::Kind::boolean) {
        throw Error(ErrorCode::invalid_argument,
                    where + " gives the folding \"" + key + "\" as " + kind_of(setting), expected,
                    "write \"" + key + "\": true or false");
      }
      const bool on = setting.as_bool();
      if (key == "case") {
        folding.text.case_fold = on;
      } else if (key == "diacritics") {
        folding.text.diacritics = on;
      } else if (key == "whitespace") {
        folding.text.whitespace = on;
      } else {
        folding.hyphenation = on;
      }
    } else if (key == "equivalents") {
      if (setting.kind() != JsonValue::Kind::array) {
        throw Error(ErrorCode::invalid_argument,
                    where + " gives \"equivalents\" as " + kind_of(setting), expected,
                    R"(write the classes as an array of arrays, such as [["'", "’"]])");
      }
      const JsonValue::Array& classes = setting.as_array();
      for (std::size_t c = 0; c < classes.size(); ++c) {
        const std::string what = "equivalents class " + std::to_string(c);
        const JsonValue& members = classes[c];
        if (members.kind() != JsonValue::Kind::array || members.as_array().empty()) {
          throw Error(
              ErrorCode::invalid_argument,
              where + " gives " + what +
                  (members.kind() == JsonValue::Kind::array ? " empty" : " as " + kind_of(members)),
              expected, "write each class as a non-empty array of one-character strings");
        }
        const char32_t first = character(members.as_array().front(), where, expected, what);
        for (const JsonValue& member : members.as_array()) {
          const char32_t code_point = character(member, where, expected, what);
          once(code_point, what);
          folding.text.equivalent[code_point] = first;
        }
      }
    } else if (key == "ignore") {
      if (setting.kind() != JsonValue::Kind::array) {
        throw Error(ErrorCode::invalid_argument, where + " gives \"ignore\" as " + kind_of(setting),
                    expected, "write the ignored characters as an array, such as [\"\\u00AD\"]");
      }
      for (const JsonValue& member : setting.as_array()) {
        const char32_t code_point = character(member, where, expected, "ignore");
        once(code_point, "ignore");
        folding.text.ignored.insert(code_point);
      }
    } else {
      throw Error(ErrorCode::invalid_argument, where + " names the unknown folding \"" + key + "\"",
                  expected, "use only " + std::string(keys));
    }
  }
  return folding;
}

}  // namespace pdfkit::api
