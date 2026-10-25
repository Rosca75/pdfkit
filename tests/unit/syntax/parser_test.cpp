// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::Parser: direct objects of every kind, references by
// lookahead, indirect objects, the stream /Length rule with a right, short,
// long and indirect length, a missing endstream and a missing endobj, and the
// nesting limit.

#include "syntax/parser.hpp"

#include "platform/byte_span.hpp"
#include "support/require_value.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"

#include <doctest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using pdfkit::platform::as_bytes;
using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::syntax::Parser;
using pdfkit::test::require_value;

Object parse(const std::string& text, std::vector<std::string>* repairs_out = nullptr) {
  std::vector<std::string> repairs;
  const Object object = Parser(as_bytes(text)).parse_direct(0, repairs);
  if (repairs_out != nullptr) {
    *repairs_out = std::move(repairs);
  }
  return object;
}

std::string data_of(const std::string& file, const Object& object) {
  const pdfkit::syntax::Stream* stream = object.stream();
  REQUIRE(stream != nullptr);
  return file.substr(stream->data_offset(), stream->data_length());
}

TEST_CASE("direct objects of every kind") {
  CHECK(parse("null").is_null());
  CHECK(parse("true").boolean() == true);
  CHECK(parse("false").boolean() == false);
  CHECK(parse("42").integer() == 42);
  CHECK_FALSE(parse("4.5").integer().has_value());
  CHECK(parse("4.5").number() == doctest::Approx(4.5));
  CHECK(parse("(a)").string()->bytes == "a");
  CHECK_FALSE(parse("(a)").string()->hex);
  CHECK(parse("<61>").string()->hex);
  CHECK(parse("/Type").is_name("Type"));
  const Object array = parse("[1 (two) /three [4] << /Five 5 >>]");
  REQUIRE(array.array() != nullptr);
  REQUIRE(array.array()->size() == 5U);
  CHECK((*array.array())[3].array()->size() == 1U);
  CHECK((*array.array())[4].dictionary()->get("Five").integer() == 5);
}

TEST_CASE("references are found by a two-token lookahead") {
  const Object reference = parse("12 0 R");
  CHECK(require_value(reference.reference()).number == 12U);
  CHECK(require_value(reference.reference()).generation == 0U);
  // "2 0 3 R" is the integer 2 followed by the reference "0 3 R".
  const Object array = parse("[1 0 R 2 0 3 R 4]");
  REQUIRE(array.array()->size() == 4U);
  CHECK(require_value((*array.array())[0].reference()).number == 1U);
  CHECK((*array.array())[1].integer() == 2);
  CHECK(require_value((*array.array())[2].reference()).number == 0U);
  CHECK(require_value((*array.array())[2].reference()).generation == 3U);
  CHECK((*array.array())[3].integer() == 4);
  CHECK(parse("[5 0]").array()->size() == 2U);
  // A real or a negative number is not an object number.
  CHECK(parse("[1.0 0 R]").array()->size() == 3U);
  CHECK(parse("[-1 0 R]").array()->size() == 3U);
}

TEST_CASE("a dictionary keeps its order and resolves a duplicate key last-wins") {
  const Object object = parse("<< /B 1 /A 2 /B 3 >>");
  const Dictionary& dictionary = *object.dictionary();
  REQUIRE(dictionary.size() == 2U);
  CHECK(dictionary.entries()[0].first == "B");
  CHECK(dictionary.entries()[0].second.integer() == 3);
  CHECK(dictionary.entries()[1].first == "A");
  CHECK(dictionary.get("Missing").is_null());
}

TEST_CASE("faults inside a container are skipped and recorded") {
  std::vector<std::string> repairs;
  const Object object = parse("<< /A 1 2 /B (x) >>", &repairs);
  CHECK(object.dictionary()->get("A").integer() == 1);
  CHECK(object.dictionary()->get("B").string()->bytes == "x");
  CHECK_FALSE(repairs.empty());
  repairs.clear();
  const Object open = parse("<< /A [1 2", &repairs);
  CHECK(open.dictionary()->get("A").array()->size() == 2U);
  CHECK_FALSE(repairs.empty());
}

TEST_CASE("containers nested past the limit end there") {
  std::string deep(1000, '[');
  std::vector<std::string> repairs;
  const Object object = parse(deep, &repairs);
  CHECK(object.array() != nullptr);
  CHECK_FALSE(repairs.empty());
}

TEST_CASE("an indirect object at an offset") {
  const std::string file = "junk 7 0 obj\n<< /Type /Example /Kid 8 0 R >>\nendobj\n";
  const auto indirect = require_value(Parser(as_bytes(file)).parse_indirect(5, {}));
  CHECK(indirect.reference.number == 7U);
  CHECK(indirect.object.dictionary()->get("Type").is_name("Example"));
  CHECK(indirect.repairs.empty());
  CHECK(indirect.start == 5U);
  CHECK(file.substr(indirect.end - 6, 6) == "endobj");
  CHECK_FALSE(Parser(as_bytes(file)).parse_indirect(0, {}).has_value());
  CHECK_FALSE(Parser(as_bytes(file)).parse_indirect(6, {}).has_value());
}

TEST_CASE("a missing endobj ends the object at the next token and is recorded") {
  const std::string file = "1 0 obj\n<< /A 1 >>\n2 0 obj\n(x)\nendobj\n";
  const auto indirect = require_value(Parser(as_bytes(file)).parse_indirect(0, {}));
  CHECK(indirect.object.dictionary()->get("A").integer() == 1);
  CHECK(indirect.repairs.size() == 1U);
  CHECK(file.substr(indirect.end, 7) == "2 0 obj");
}

TEST_CASE("a stream whose /Length ends at endstream is trusted") {
  const std::string file = "4 0 obj\n<< /Length 5 >>\nstream\r\nhello\r\nendstream\nendobj\n";
  const auto indirect = require_value(Parser(as_bytes(file)).parse_indirect(0, {}));
  CHECK(data_of(file, indirect.object) == "hello");
  CHECK(indirect.repairs.empty());
  CHECK(indirect.object.stream()->owner().number == 4U);
  CHECK(indirect.object.stream()->dictionary().get("Length").integer() == 5);
}

TEST_CASE("the data starts after one CR LF, one LF or one lone CR") {
  for (const std::string eol : {"\r\n", "\n", "\r"}) {
    const std::string file = "1 0 obj <</Length 3>> stream" + eol + "abc\nendstream endobj";
    const auto indirect = require_value(Parser(as_bytes(file)).parse_indirect(0, {}));
    CHECK(data_of(file, indirect.object) == "abc");
    CHECK(indirect.repairs.empty());
  }
}

TEST_CASE("a short or long /Length gives way to the endstream found by scanning") {
  for (const std::string length : {"3", "9", "-1", "999999"}) {
    const std::string file =
        "1 0 obj <</Length " + length + ">> stream\nhello\r\nendstream\nendobj\n";
    const auto indirect = require_value(Parser(as_bytes(file)).parse_indirect(0, {}));
    CHECK(data_of(file, indirect.object) == "hello");
    CHECK(indirect.repairs.size() == 1U);
    CHECK(file.substr(indirect.end - 6, 6) == "endobj");
  }
}

TEST_CASE("a missing /Length is recovered the same way") {
  const std::string file = "1 0 obj <<>> stream\nhello\nendstream\nendobj\n";
  const auto indirect = require_value(Parser(as_bytes(file)).parse_indirect(0, {}));
  CHECK(data_of(file, indirect.object) == "hello");
  CHECK(indirect.repairs.size() == 1U);
}

TEST_CASE("an indirect /Length is answered by the resolver") {
  const std::string file = "1 0 obj <</Length 2 0 R>> stream\nhello\nendstream\nendobj\n";
  Object::Reference asked{};
  const auto indirect =
      require_value(Parser(as_bytes(file)).parse_indirect(0, [&](Object::Reference reference) {
        asked = reference;
        return std::optional<std::int64_t>(5);
      }));
  CHECK(asked.number == 2U);
  CHECK(data_of(file, indirect.object) == "hello");
  CHECK(indirect.repairs.empty());
  // A resolver that cannot answer (a cycle, a missing object) falls back to the scan.
  const auto unknown =
      require_value(Parser(as_bytes(file)).parse_indirect(0, [](Object::Reference) {
        return std::optional<std::int64_t>();
      }));
  CHECK(data_of(file, unknown.object) == "hello");
  CHECK(unknown.repairs.size() == 1U);
}

TEST_CASE("a missing endstream ends the data before endobj and is recorded") {
  const std::string file = "1 0 obj <</Length 50>> stream\nhello\nendobj\n2 0 obj 5 endobj";
  const auto indirect = require_value(Parser(as_bytes(file)).parse_indirect(0, {}));
  CHECK(data_of(file, indirect.object) == "hello");
  CHECK_FALSE(indirect.repairs.empty());
  CHECK(file.substr(indirect.end, 7) == "\n2 0 ob");
}

TEST_CASE("a stream with neither endstream nor endobj runs to the end of the data") {
  const std::string file = "1 0 obj <</Length 50>> stream\nhello";
  const auto indirect = require_value(Parser(as_bytes(file)).parse_indirect(0, {}));
  CHECK(data_of(file, indirect.object) == "hello");
  CHECK_FALSE(indirect.repairs.empty());
}

TEST_CASE("find_keyword finds whole tokens only") {
  const std::string text = "xendstream endstreamx endstream";
  const auto found = Parser::find_keyword(as_bytes(text), 0, "endstream");
  CHECK(require_value(found) == 22U);
  CHECK_FALSE(Parser::find_keyword(as_bytes(text), 23, "endstream").has_value());
}

}  // namespace
