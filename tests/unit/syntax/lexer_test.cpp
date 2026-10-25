// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::Lexer: every token class of docs/DESIGN.md section 1.1 with
// its edge cases, the leniencies marked repaired, and the byte positions each
// token reports.

#include "syntax/lexer.hpp"

#include "platform/byte_span.hpp"

#include <doctest.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

using pdfkit::platform::as_bytes;
using pdfkit::syntax::Lexer;
using Kind = Lexer::TokenKind;

std::vector<Lexer::Token> lex_all(std::string_view text) {
  Lexer lexer(as_bytes(text));
  std::vector<Lexer::Token> tokens;
  for (Lexer::Token token = lexer.next(); token.kind != Kind::end; token = lexer.next()) {
    tokens.push_back(token);
  }
  return tokens;
}

Lexer::Token lex_one(std::string_view text) {
  const std::vector<Lexer::Token> tokens = lex_all(text);
  REQUIRE(tokens.size() == 1U);
  return tokens.front();
}

TEST_CASE("white space and comments separate tokens") {
  const auto tokens = lex_all(std::string("a\x00 b\tc\nd\x0C e\r\nf%comment g\rh", 26));
  REQUIRE(tokens.size() == 7U);
  CHECK(tokens[0].text == "a");
  CHECK(tokens[5].text == "f");
  CHECK(tokens[6].text == "h");
  CHECK(lex_all("% only a comment").empty());
  CHECK(lex_all("").empty());
}

TEST_CASE("delimiters and dictionary brackets") {
  const auto tokens = lex_all("<<[{}]>>");
  REQUIRE(tokens.size() == 6U);
  CHECK(tokens[0].kind == Kind::dict_open);
  CHECK(tokens[1].kind == Kind::array_open);
  CHECK(tokens[2].kind == Kind::brace_open);
  CHECK(tokens[3].kind == Kind::brace_close);
  CHECK(tokens[4].kind == Kind::array_close);
  CHECK(tokens[5].kind == Kind::dict_close);
  CHECK(lex_one(")").kind == Kind::stray);
  CHECK(lex_one(">").kind == Kind::stray);
}

TEST_CASE("numbers in every form") {
  CHECK(lex_one("123").kind == Kind::integer);
  CHECK(lex_one("123").integer == 123);
  CHECK(lex_one("+17").integer == 17);
  CHECK(lex_one("-98").integer == -98);
  CHECK(lex_one("0").integer == 0);
  CHECK(lex_one("34.5").kind == Kind::real);
  CHECK(lex_one("34.5").real == doctest::Approx(34.5));
  CHECK(lex_one("-3.62").real == doctest::Approx(-3.62));
  CHECK(lex_one("+123.6").real == doctest::Approx(123.6));
  CHECK(lex_one("4.").kind == Kind::real);
  CHECK(lex_one("4.").real == doctest::Approx(4.0));
  CHECK(lex_one("-.002").real == doctest::Approx(-0.002));
  CHECK(lex_one(".5").real == doctest::Approx(0.5));
  CHECK(lex_one("0.0").real == doctest::Approx(0.0));
}

TEST_CASE("a doubled sign and a sign alone read as viewers read them") {
  const Lexer::Token doubled = lex_one("--5");
  CHECK(doubled.kind == Kind::integer);
  CHECK(doubled.integer == -5);
  CHECK(doubled.repaired);
  const Lexer::Token alone = lex_one("-");
  CHECK(alone.kind == Kind::integer);
  CHECK(alone.integer == 0);
  CHECK(alone.repaired);
  CHECK(lex_one(".").kind == Kind::integer);
}

TEST_CASE("an integer past 64 bits is read as a real") {
  const Lexer::Token big = lex_one("123456789012345678901234567890");
  CHECK(big.kind == Kind::real);
  CHECK(big.real == doctest::Approx(1.2345678901234568e29));
  CHECK(lex_one("9223372036854775807").kind == Kind::integer);
  CHECK(lex_one("-9223372036854775808").kind == Kind::integer);
  CHECK(lex_one("-9223372036854775808").integer == INT64_MIN);
  CHECK(lex_one("9223372036854775808").kind == Kind::real);
}

TEST_CASE("a number followed by regular bytes ends at the first byte that is not numeric") {
  const auto tokens = lex_all("12abc");
  REQUIRE(tokens.size() == 2U);
  CHECK(tokens[0].integer == 12);
  CHECK(tokens[1].is_keyword("abc"));
}

TEST_CASE("literal strings with every escape") {
  CHECK(lex_one("(abc)").text == "abc");
  CHECK(lex_one("(abc)").kind == Kind::literal_string);
  CHECK(lex_one("(a(b)c)").text == "a(b)c");
  CHECK(lex_one("(a((b))c)").text == "a((b))c");
  CHECK(lex_one(R"((\n\r\t\b\f\(\)\\))").text == "\n\r\t\b\f()\\");
  CHECK(lex_one(R"((\101\60\7))").text == std::string("A0\x07"));
  CHECK(lex_one(R"((\0053))").text == std::string("\x05"
                                                  "3"));
  CHECK(lex_one(R"((\1234))").text == "S4");
  // Above \377 the low byte is kept.
  CHECK(lex_one(R"((\400))").text == std::string(1, '\0'));
  CHECK(lex_one(R"((\777))").text == std::string(1, '\xFF'));
  // An unknown escape drops the backslash.
  CHECK(lex_one(R"((\q))").text == "q");
  // A backslash before an end of line continues the line.
  CHECK(lex_one("(ab\\\ncd)").text == "abcd");
  CHECK(lex_one("(ab\\\r\ncd)").text == "abcd");
  CHECK(lex_one("(ab\\\rcd)").text == "abcd");
  // Every unescaped end of line reads as one LF.
  CHECK(lex_one("(a\r\nb\rc\nd)").text == "a\nb\nc\nd");
  CHECK(lex_one("()").text.empty());
  CHECK_FALSE(lex_one("(abc)").repaired);
}

TEST_CASE("an unterminated literal string ends at the end of the data and is marked repaired") {
  const Lexer::Token token = lex_one("(abc(def)");
  CHECK(token.kind == Kind::literal_string);
  CHECK(token.text == "abc(def)");
  CHECK(token.repaired);
  CHECK(token.end == 9U);
  CHECK(lex_one("(abc\\").repaired);
}

TEST_CASE("hexadecimal strings") {
  const Lexer::Token token = lex_one("<4E6F762073686D6F7A206B6120706F702E>");
  CHECK(token.kind == Kind::hex_string);
  CHECK(token.text == "Nov shmoz ka pop.");
  CHECK(lex_one("<41 42\n43>").text == "ABC");
  CHECK(lex_one("<6162c3>").text == "ab\xC3");
  // An odd final digit is padded with 0.
  CHECK(lex_one("<901FA>").text == std::string("\x90\x1F\xA0"));
  CHECK(lex_one("<>").text.empty());
  CHECK_FALSE(lex_one("<41>").repaired);
}

TEST_CASE("a non-hexadecimal byte ends a hexadecimal string and marks it repaired") {
  const auto tokens = lex_all("<41zz> 5");
  REQUIRE(!tokens.empty());
  CHECK(tokens[0].kind == Kind::hex_string);
  CHECK(tokens[0].text == "A");
  CHECK(tokens[0].repaired);
  CHECK(lex_one("<4142").repaired);
}

TEST_CASE("names with #xx escapes") {
  CHECK(lex_one("/Name1").text == "Name1");
  CHECK(lex_one("/Name1").kind == Kind::name);
  CHECK(lex_one("/A;Name_With-Various***Characters?").text == "A;Name_With-Various***Characters?");
  CHECK(lex_one("/1.2").text == "1.2");
  CHECK(lex_one("/Lime#20Green").text == "Lime Green");
  CHECK(lex_one("/paired#28#29parentheses").text == "paired()parentheses");
  CHECK(lex_one("/The_Key_of_F#23_Minor").text == "The_Key_of_F#_Minor");
  CHECK(lex_one("/A#42").text == "AB");
  // A lone '#' is kept as written (pdf.js reads it so).
  const Lexer::Token lone = lex_one("/A#G");
  CHECK(lone.text == "A#G");
  CHECK(lone.repaired);
  CHECK(lex_one("/A#").text == "A#");
  CHECK(lex_one("/A#4").text == "A#4");
}

TEST_CASE("the empty name is a valid name") {
  const auto tokens = lex_all("/ /A");
  REQUIRE(tokens.size() == 2U);
  CHECK(tokens[0].kind == Kind::name);
  CHECK(tokens[0].text.empty());
  CHECK(tokens[1].text == "A");
  const auto adjacent = lex_all("//A");
  REQUIRE(adjacent.size() == 2U);
  CHECK(adjacent[0].text.empty());
}

TEST_CASE("keywords") {
  const auto tokens =
      lex_all("true false null obj endobj stream endstream R xref trailer startxref");
  REQUIRE(tokens.size() == 11U);
  CHECK(tokens[0].is_keyword("true"));
  CHECK(tokens[7].is_keyword("R"));
  CHECK(tokens[10].is_keyword("startxref"));
  CHECK(lex_one("T*").is_keyword("T*"));
  CHECK(lex_one("'").is_keyword("'"));
}

TEST_CASE("tokens report their byte ranges") {
  const std::string text = "  12 0 obj <</A (x)>> endobj";
  const auto tokens = lex_all(text);
  REQUIRE(tokens.size() == 8U);
  CHECK(tokens[0].start == 2U);
  CHECK(tokens[0].end == 4U);
  CHECK(tokens[2].start == 7U);
  CHECK(tokens[2].end == 10U);
  CHECK(tokens[3].start == 11U);
  CHECK(tokens[3].end == 13U);
  CHECK(tokens[5].start == 16U);
  CHECK(tokens[5].end == 19U);
  CHECK(tokens[7].start == 22U);
  CHECK(tokens[7].end == text.size());
}

TEST_CASE("seek repositions the walk") {
  const std::string text = "1 2 3";
  Lexer lexer(as_bytes(text));
  CHECK(lexer.next().integer == 1);
  lexer.seek(4);
  CHECK(lexer.next().integer == 3);
  CHECK(lexer.next().kind == Kind::end);
  lexer.seek(100);
  CHECK(lexer.next().kind == Kind::end);
}

}  // namespace
