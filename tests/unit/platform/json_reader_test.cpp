// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of platform::JsonReader and JsonValue: every JSON type, strictness
// (RFC 8259 and nothing more), the depth and size limits, and errors that name
// the byte offset, what was expected and how to fix the input.

#include "platform/json_reader.hpp"

#include "platform/error.hpp"
#include "platform/json_value.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;

Error error_of(std::string_view text) {
  try {
    (void)JsonReader::parse(text);
  } catch (const Error& error) {
    return error;
  }
  FAIL("the input was accepted: ", std::string(text));
  return {ErrorCode::internal, "unreachable", "unreachable", "unreachable"};
}

TEST_CASE("every JSON type is read") {
  const JsonValue value = JsonReader::parse(
      R"( {"n": null, "t": true, "f": false, "i": -42, "d": 2.5e1, "s": "a\u00e9\ud83d\ude00",)"
      R"( "a": [1, [2], {}], "o": {"k": "v"}} )");
  REQUIRE(value.kind() == JsonValue::Kind::object);
  CHECK(value.find("n")->kind() == JsonValue::Kind::null);
  CHECK(value.find("t")->as_bool());
  CHECK_FALSE(value.find("f")->as_bool());
  CHECK(value.find("i")->as_int64() == std::int64_t{-42});
  CHECK(value.find("d")->as_number() == doctest::Approx(25.0));
  CHECK_FALSE(value.find("d")->as_int64().has_value());
  CHECK(value.find("s")->as_string() == "a\xC3\xA9\xF0\x9F\x98\x80");
  CHECK(value.find("a")->as_array().size() == 3U);
  CHECK(value.find("a")->as_array()[1].as_array()[0].as_int64() == std::int64_t{2});
  CHECK(value.find("o")->find("k")->as_string() == "v");
  CHECK(value.find("missing") == nullptr);
  CHECK(value.as_object().front().first == "n");
}

TEST_CASE("a large integer keeps its exact value") {
  const JsonValue value = JsonReader::parse("9007199254740993");
  CHECK(value.as_int64() == std::int64_t{9007199254740993});
  CHECK_FALSE(JsonReader::parse("1e3").as_int64().has_value());
  CHECK_FALSE(JsonReader::parse("99999999999999999999").as_int64().has_value());
}

TEST_CASE("input outside strict JSON is refused") {
  for (const char* text : {"",
                           "   ",
                           "{",
                           "[1,]",
                           "{\"a\":1,}",
                           "{'a':1}",
                           "01",
                           "1.",
                           ".5",
                           "+1",
                           "-",
                           "1e",
                           "NaN",
                           "Infinity",
                           "tru",
                           "nul",
                           R"("\x")",
                           R"("\u12")",
                           R"("\ud800")",
                           R"("\udc00")",
                           "{\"a\" 1}",
                           "{1:2}",
                           "[1 2]",
                           "// c\n1",
                           "1 2",
                           "\"a\nb\"",
                           "\xEF\xBB\xBF{}",
                           "\"\xFF\"",
                           R"({"a":1,"a":2})"}) {
    CAPTURE(text);
    const Error error = error_of(text);
    CHECK(error.code() == ErrorCode::invalid_argument);
  }
}

TEST_CASE("an error names the byte offset, what was expected and the fix") {
  const Error error = error_of(R"({"pages": [1, 2,]})");
  CHECK(error.failed().find("byte 16") != std::string::npos);
  CHECK_FALSE(error.expected().empty());
  CHECK_FALSE(error.fix().empty());
}

TEST_CASE("nesting deeper than 64 levels is refused") {
  const std::string ok = std::string(64, '[') + std::string(64, ']');
  CHECK(JsonReader::parse(ok).kind() == JsonValue::Kind::array);
  const std::string deep = std::string(65, '[') + std::string(65, ']');
  const Error error = error_of(deep);
  CHECK(error.failed().find("64") != std::string::npos);
}

TEST_CASE("input over the size limit is refused before it is read") {
  JsonReader::Limits limits;
  limits.max_bytes = 8;
  CHECK(JsonReader::parse("[1,2,3]", limits).as_array().size() == 3U);
  try {
    (void)JsonReader::parse("[1,2,3,4]", limits);
    FAIL("oversized input was accepted");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::invalid_argument);
    CHECK(error.failed().find("9 bytes") != std::string::npos);
  }
}

TEST_CASE("asking a value for the wrong type is an internal error") {
  const JsonValue value = JsonReader::parse("\"text\"");
  try {
    (void)value.as_array();
    FAIL("a string was read as an array");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::internal);
  }
}

}  // namespace
