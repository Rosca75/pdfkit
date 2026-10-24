// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of platform::JsonWriter: string escaping per RFC 8259, the number
// forms (shortest round trip, fixed for rounded geometry), insertion order,
// misuse refused, and byte-identical output for identical input.

#include "platform/json_writer.hpp"

#include "platform/error.hpp"

#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::JsonWriter;

std::string one_string(std::string_view text) {
  JsonWriter writer;
  writer.string(text);
  return writer.take();
}

std::string one_number(double value) {
  JsonWriter writer;
  writer.number(value);
  return writer.take();
}

std::string one_fixed(double value, int decimals) {
  JsonWriter writer;
  writer.fixed(value, decimals);
  return writer.take();
}

TEST_CASE("strings are escaped as RFC 8259 requires") {
  CHECK(one_string("plain") == R"("plain")");
  CHECK(one_string("a\"b\\c") == R"("a\"b\\c")");
  CHECK(one_string("\n\r\t\b\f") == R"("\n\r\t\b\f")");
  CHECK(one_string(std::string_view("\x01\x1f", 2)) == R"("\u0001\u001f")");
  CHECK(one_string(std::string_view("\0", 1)) == R"("\u0000")");
  CHECK(one_string("/") == R"("/")");
  CHECK(one_string("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80") ==
        "\"caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\"");
}

TEST_CASE("invalid UTF-8 is replaced by U+FFFD so the output stays valid JSON") {
  CHECK(one_string("a\xFF"
                   "b") ==
        "\"a\xEF\xBF\xBD"
        "b\"");
  CHECK(one_string("\xC3") == "\"\xEF\xBF\xBD\"");
  CHECK(one_string("\xED\xA0\x80") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"");
  CHECK(one_string("\xC0\xAF") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\"");
}

TEST_CASE("numbers use the shortest form that reads back to the same double") {
  CHECK(one_number(0.0) == "0");
  CHECK(one_number(-0.0) == "0");
  CHECK(one_number(1.0) == "1");
  CHECK(one_number(0.1) == "0.1");
  CHECK(one_number(-2.5) == "-2.5");
  CHECK(one_number(1e21) == "1e+21");
  CHECK(one_number(1.0 / 3.0) == "0.3333333333333333");
}

TEST_CASE("fixed numbers round to the given decimals and never print -0") {
  CHECK(one_fixed(12.3456, 2) == "12.35");
  CHECK(one_fixed(-0.001, 2) == "0.00");
  CHECK(one_fixed(5.0, 2) == "5.00");
  CHECK(one_fixed(-7.125, 1) == "-7.1");
}

TEST_CASE("non-finite numbers are refused") {
  JsonWriter writer;
  CHECK_THROWS_AS(writer.number(std::numeric_limits<double>::quiet_NaN()), Error);
  JsonWriter other;
  CHECK_THROWS_AS(other.fixed(std::numeric_limits<double>::infinity(), 2), Error);
}

TEST_CASE("objects keep insertion order and nest") {
  JsonWriter writer;
  writer.begin_object();
  writer.key("zeta").integer(-3);
  writer.key("alpha").unsigned_integer(std::numeric_limits<std::uint64_t>::max());
  writer.key("list").begin_array();
  writer.boolean(true).boolean(false).null();
  writer.begin_object().end_object();
  writer.begin_array().end_array();
  writer.end_array();
  writer.end_object();
  CHECK(writer.complete());
  CHECK(writer.take() ==
        R"({"zeta":-3,"alpha":18446744073709551615,"list":[true,false,null,{},[]]})");
}

TEST_CASE("misuse is refused rather than producing invalid JSON") {
  JsonWriter value_without_key;
  value_without_key.begin_object();
  CHECK_THROWS_AS(value_without_key.integer(1), Error);

  JsonWriter key_in_array;
  key_in_array.begin_array();
  CHECK_THROWS_AS(key_in_array.key("x"), Error);

  JsonWriter mismatched;
  mismatched.begin_array();
  CHECK_THROWS_AS(mismatched.end_object(), Error);

  JsonWriter two_values;
  two_values.integer(1);
  CHECK_THROWS_AS(two_values.integer(2), Error);

  JsonWriter unfinished;
  unfinished.begin_object();
  CHECK_FALSE(unfinished.complete());
  CHECK_THROWS_AS((void)unfinished.take(), Error);
}

TEST_CASE("identical input gives identical bytes") {
  auto write = [] {
    JsonWriter writer;
    writer.begin_object();
    writer.key("x").fixed(72.004999, 2);
    writer.key("y").number(841.89);
    writer.key("text").string("Name \xE2\x80\x94 \"quoted\"");
    writer.end_object();
    return writer.take();
  };
  CHECK(write() == write());
}

}  // namespace
