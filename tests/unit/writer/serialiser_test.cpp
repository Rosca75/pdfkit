// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the serialiser: every object kind written and read back through
// the library's own parser is the object it was; a string keeps its literal
// or hexadecimal form; the escapes of strings and names; numbers without an
// exponent; dictionaries in their read order; references renumbered or
// written as null.

#include "writer/serialiser.hpp"

#include "platform/byte_span.hpp"
#include "platform/error.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "syntax/parser.hpp"
#include "syntax/stream.hpp"

#include <doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::writer::Serialiser;

std::optional<std::uint32_t> same(Object::Reference reference) {
  return reference.number;
}

std::string text(const Object& value) {
  std::string out;
  Serialiser::write(out, value, &same);
  return out;
}

Object parse(const std::string& written) {
  std::vector<std::string> repairs;
  const pdfkit::syntax::Parser parser(pdfkit::platform::as_bytes(written));
  Object value = parser.parse_direct(0, repairs);
  CHECK(repairs.empty());
  return value;
}

void check_round_trip(const Object& value) {
  const std::string written = text(value);
  INFO(written);
  CHECK(parse(written) == value);
}

}  // namespace

TEST_CASE("every object kind round-trips through the parser") {
  check_round_trip(Object());
  check_round_trip(Object::make_boolean(true));
  check_round_trip(Object::make_boolean(false));
  check_round_trip(Object::make_integer(0));
  check_round_trip(Object::make_integer(-42));
  check_round_trip(Object::make_integer(9007199254740993));
  check_round_trip(Object::make_real(0.1));
  check_round_trip(Object::make_real(-0.5));
  check_round_trip(Object::make_real(1e-7));
  check_round_trip(Object::make_real(612.0));
  check_round_trip(Object::make_real(123456.789));
  check_round_trip(Object::make_string("Hello (world)\\ \n\r\t\b\f end"));
  check_round_trip(Object::make_string(std::string("\x00\x01\xFE\xFF\x7F", 5)));
  check_round_trip(Object::make_string(std::string("\xFE\xFF\x00J\x00o", 6), true));
  check_round_trip(Object::make_string(""));
  check_round_trip(Object::make_string("", true));
  check_round_trip(Object::make_name("Type"));
  check_round_trip(Object::make_name("A B#C(D)/E%F\xE9"));
  check_round_trip(Object::make_name(""));
  check_round_trip(Object::make_reference({12, 0}));
  check_round_trip(Object::make_array({Object::make_integer(1), Object::make_name("N"),
                                       Object::make_array({}), Object::make_string("s")}));
  Dictionary inner;
  inner.set("Z", Object::make_integer(1));
  inner.set("A", Object::make_reference({3, 0}));
  Dictionary outer;
  outer.set("Type", Object::make_name("Page"));
  outer.set("Inner", Object::make_dictionary(inner));
  outer.set("Box", Object::make_array({Object::make_integer(0), Object::make_real(0.25)}));
  check_round_trip(Object::make_dictionary(outer));
}

TEST_CASE("a string is written in the form it was read") {
  CHECK(text(Object::make_string("Hi")) == "(Hi)");
  CHECK(text(Object::make_string("Hi", true)) == "<4869>");
  CHECK(text(Object::make_string("a(b)c\\d")) == "(a\\(b\\)c\\\\d)");
  CHECK(text(Object::make_string("\n\r\t\b\f")) == "(\\n\\r\\t\\b\\f)");
  CHECK(text(Object::make_string(std::string("\x00\xFF\x7F", 3))) == "(\\000\\377\\177)");
  CHECK(text(Object::make_string(std::string("\x01", 1) + "7")) == "(\\0017)");
}

TEST_CASE("a name escapes delimiters, the number sign and bytes outside 0x21 to 0x7E") {
  CHECK(text(Object::make_name("Type")) == "/Type");
  CHECK(text(Object::make_name("A B")) == "/A#20B");
  CHECK(text(Object::make_name("x#y")) == "/x#23y");
  CHECK(text(Object::make_name("p(q)[r]<s>{t}/u%v")) == "/p#28q#29#5Br#5D#3Cs#3E#7Bt#7D#2Fu#25v");
  CHECK(text(Object::make_name("caf\xE9")) == "/caf#E9");
  CHECK(text(Object::make_name("")) == "/");
}

TEST_CASE("numbers are written without an exponent, reals with a decimal point") {
  CHECK(text(Object::make_integer(-17)) == "-17");
  CHECK(text(Object::make_integer(9223372036854775807)) == "9223372036854775807");
  CHECK(text(Object::make_real(0.1)) == "0.1");
  CHECK(text(Object::make_real(-0.5)) == "-0.5");
  CHECK(text(Object::make_real(1e-7)) == "0.0000001");
  CHECK(text(Object::make_real(612.0)) == "612.0");
  CHECK(text(Object::make_real(-0.0)) == "0.0");
  CHECK(text(Object::make_real(1e20)) == "100000000000000000000.0");
}

TEST_CASE("dictionaries keep their key order and containers use one space") {
  Dictionary dictionary;
  dictionary.set("Z", Object::make_integer(1));
  dictionary.set("A", Object::make_array({Object::make_integer(1), Object::make_integer(2)}));
  dictionary.set("M", Object::make_reference({7, 0}));
  CHECK(text(Object::make_dictionary(dictionary)) == "<< /Z 1 /A [1 2] /M 7 0 R >>");
  CHECK(text(Object::make_dictionary(Dictionary())) == "<< >>");
}

TEST_CASE("references are renumbered and a missing one is written as null") {
  const Serialiser::Numbering numbering =
      [](Object::Reference reference) -> std::optional<std::uint32_t> {
    if (reference.number == 5) {
      return 1;
    }
    return std::nullopt;
  };
  std::string out;
  Serialiser::write(
      out, Object::make_array({Object::make_reference({5, 0}), Object::make_reference({6, 2})}),
      numbering);
  CHECK(out == "[1 0 R null]");
}

TEST_CASE("a direct stream is refused as a defect") {
  std::string out;
  const Object stream = Object::make_stream(pdfkit::syntax::Stream(Dictionary(), 0, 0, {1, 0}));
  CHECK_THROWS_AS(Serialiser::write(out, stream, &same), pdfkit::platform::Error);
}
