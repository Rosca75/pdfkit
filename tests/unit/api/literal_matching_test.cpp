// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The literal tool of ABI 2 (docs/ABI.md, the pdfkit_redact_text and
// pdfkit_find sections): a folding the caller leaves out folds nothing, in
// pdfkit_find, the search and occurrence forms of pdfkit_redact_text and
// pdfkit_check alike; the caller's character classes and ignored characters
// apply wherever the folding does, and are refused when they name a
// character twice or a character that is not one code point; pdfkit_find
// reports the letters glued to each occurrence; and the overlap rule's tie
// (docs/DESIGN.md section 11) gives two searches that start on one glyph a
// determined winner.

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "redact/support/redaction.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;

std::string take(pdfkit_buffer& buffer) {
  std::string text(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  return text;
}

struct Answer {
  pdfkit_status status = PDFKIT_OK;
  std::string json;
};

Answer find(const std::string& file, const std::string& values) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                      &doc, &out) == PDFKIT_OK);
  (void)take(out);
  Answer answer;
  answer.status = pdfkit_find(doc, values.c_str(), &out);
  answer.json = take(out);
  pdfkit_close(doc);
  return answer;
}

Answer check(const std::string& file, const std::string& strings) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  Answer answer;
  answer.status = pdfkit_check(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(),
                               strings.c_str(), &out);
  answer.json = take(out);
  return answer;
}

// The count pdfkit_find answers for one value with folding (an object's
// members, or empty for none).
std::int64_t count(const std::string& file, const std::string& text, const std::string& folding) {
  const std::string values = R"({"values": [{"text": )" + text +
                             (folding.empty() ? "" : R"(, "folding": {)" + folding + "}") + "}]}";
  const Answer answer = find(file, values);
  INFO(values, answer.json);
  REQUIRE(answer.status == PDFKIT_OK);
  return JsonReader::parse(answer.json)
      .find("values")
      ->as_array()[0]
      .find("count")
      ->as_int64()
      .value_or(-1);
}

// Each occurrence's text and glued letters.
std::vector<std::vector<std::string>> glued(const std::string& file, const std::string& values) {
  const Answer answer = find(file, values);
  INFO(answer.json);
  REQUIRE(answer.status == PDFKIT_OK);
  std::vector<std::vector<std::string>> out;
  const JsonValue json = JsonReader::parse(answer.json);
  for (const JsonValue& occurrence :
       json.find("values")->as_array()[0].find("occurrences")->as_array()) {
    out.push_back(
        {occurrence.find("glued_before")->as_string(), occurrence.find("text")->as_string(),
         occurrence.find("glued_after")->as_string(), occurrence.find("order")->as_string()});
  }
  return out;
}

// text with every run of white space read as one space, because the
// placeholder is a block of its own.
std::string spaced(const std::string& text) {
  std::string out;
  for (const char value : text) {
    const bool white = value == ' ' || value == '\n';
    if (!white) {
      out.push_back(value);
    } else if (!out.empty() && out.back() != ' ') {
      out.push_back(' ');
    }
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

// The request's specimen: a word, a fragment inside a word and a word start.
std::string neutral() {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (The EU and the neutral Europe agreed.) Tj ET");
  return fixture.bytes();
}

}  // namespace

TEST_CASE("pdfkit_find reports the letters glued to each occurrence") {
  const std::string file = neutral();
  const auto found = glued(file, R"({"values": [{"text": "EU", "folding": {"case": true}}]})");
  REQUIRE(found.size() == 3);
  CHECK(found[0] == std::vector<std::string>{"", "EU", "", "reading"});
  CHECK(found[1] == std::vector<std::string>{"n", "eu", "tral", "reading"});
  CHECK(found[2] == std::vector<std::string>{"", "Eu", "rope", "reading"});
  // A phrase reads the word holding its first glyph and the one holding its
  // last; one that starts and ends whole words has none.
  CHECK(glued(file, R"({"values": [{"text": "U and the neu"}]})") ==
        std::vector<std::vector<std::string>>{{"E", "U and the neu", "tral", "reading"}});
  CHECK(glued(file, R"({"values": [{"text": "and the"}]})") ==
        std::vector<std::vector<std::string>>{{"", "and the", "", "reading"}});
  CHECK(glued(file, R"({"values": [{"text": "agreed"}]})") ==
        std::vector<std::vector<std::string>>{{"", "agreed", ".", "reading"}});
}

TEST_CASE(
    "an occurrence found in another order is glued to the glyphs that order reads beside it") {
  // "ravo Charli" is split by pdfkit's reading order (two blocks whose
  // lines interleave) and read whole in the drawing order.
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "BT /F1 14 Tf 72 700 Td (Alpha Bravo) Tj 0 -24 Td (Charlie Delta) Tj ET BT /F1 14 Tf 74 "
      "688 Td (Golf Hotel) Tj 0 -24 Td (India Juliet) Tj ET");
  const auto found = glued(fixture.bytes(), R"({"values": [{"text": "ravo Charli"}]})");
  REQUIRE(found.size() == 1);
  CHECK(found[0][3] == "drawing");
  CHECK(found[0][0] == "B");
  CHECK(found[0][2] == "e");
}

TEST_CASE("a folding left out folds nothing, each flag alone folds its own") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Zo\353 Quarter-) Tj 0 -12 Td (field met Jane Doe) Tj ET");
  const std::string file = fixture.bytes();
  CHECK(count(file, R"("zoë")", "") == 0);
  CHECK(count(file, R"("zoë")", R"("case": true)") == 1);
  CHECK(count(file, R"("Zoe")", "") == 0);
  CHECK(count(file, R"("Zoe")", R"("diacritics": true)") == 1);
  CHECK(count(file, R"("Jane Doe")", "") == 0);
  CHECK(count(file, R"("Jane Doe")", R"("whitespace": true)") == 1);
  CHECK(count(file, R"("Quarterfield")", "") == 0);
  CHECK(count(file, R"("Quarterfield")", R"("hyphenation": true)") == 1);
  // An empty folding object is the same as none.
  CHECK(count(file, R"("zoë")", " ") == 0);
  // The redaction's search and pdfkit_check read the same default.
  const pdfkit::test::Redaction literal =
      pdfkit::test::redact(file, R"({"matches": [{"search": "jane doe"}]})");
  REQUIRE(literal.match_status == PDFKIT_OK);
  CHECK(literal.matches.find(R"("count":0)") != std::string::npos);
  CHECK(check(file, R"({"strings": [{"text": "jane doe"}]})").json.find(R"("count":0)") !=
        std::string::npos);
  CHECK(check(file, R"({"strings": [{"text": "jane doe", "folding": {"case": true}}]})")
            .json.find(R"("count":0)") == std::string::npos);
}

TEST_CASE("the caller's character classes and ignored characters are the folding's") {
  pdfkit::test::TextDocument fixture;
  // F2 reads each two-byte code as its Unicode value: O’Brien with U+2019,
  // Smi<soft hyphen>th, and Jane Doe drawn as two words.
  fixture.page(
      "BT /F2 10 Tf 72 700 Td <004F20190042007200690065006E> Tj 0 -12 Td "
      "<0053006D006900AD00740068> Tj ET BT /F1 10 Tf 72 650 Td (Jane Doe) Tj ET");
  const std::string file = fixture.bytes();
  const std::string apostrophes = R"("equivalents": [["'", "’", "‘", "ʼ", "`", "´"]])";
  CHECK(count(file, R"("O'Brien")", "") == 0);
  CHECK(count(file, R"("O'Brien")", apostrophes) == 1);
  CHECK(count(file, R"("OʼBrien")", apostrophes) == 1);
  // With case folding the class still holds.
  CHECK(count(file, R"("o'brien")", apostrophes + R"(, "case": true)") == 1);
  CHECK(count(file, R"("Smith")", "") == 0);
  CHECK(count(file, R"("Smith")", R"("ignore": ["­"])") == 1);
  CHECK(count(file, R"("Smi­th")", R"("ignore": ["­"])") == 1);
  CHECK(count(file, R"("Jane Doe")", "") == 0);
  CHECK(count(file, R"("Jane Doe")", R"("equivalents": [[" ", " ", " "]])") == 1);
  // A search with the classes removes it, and pdfkit_check with them finds
  // nothing left.
  const pdfkit::test::Redaction redacted = pdfkit::test::redact(
      file, R"({"matches": [{"search": "O'Brien", "folding": {)" + apostrophes + "}}]}");
  INFO(redacted.matches, redacted.report);
  REQUIRE(redacted.match_status == PDFKIT_OK);
  REQUIRE(redacted.write_status == PDFKIT_OK);
  CHECK(redacted.matches.find(R"("count":1)") != std::string::npos);
  CHECK(pdfkit::test::page_text(redacted.pdf).find("Brien") == std::string::npos);
  const std::string strings =
      R"({"strings": [{"text": "O'Brien", "folding": {)" + apostrophes + "}}]}";
  CHECK(check(file, strings).json.find(R"("count":0)") == std::string::npos);
  CHECK(check(redacted.pdf, strings).json.find(R"("count":0)") != std::string::npos);
  // An occurrence's narrowing reads the classes too.
  const pdfkit::test::Redaction located = pdfkit::test::redact(
      file,
      R"({"matches": [{"occurrence": {"text": "O'Brien", "page": 1, "index": 0, "folding": {)" +
          apostrophes + "}}}]}");
  CHECK(located.match_status == PDFKIT_OK);
}

TEST_CASE("character classes that name a character twice or not one character are refused") {
  const std::string file = neutral();
  for (const std::string bad : {
           R"({"equivalents": [["a", "b"], ["b", "c"]]})",
           R"({"equivalents": [["a", "a"]]})",
           R"({"equivalents": [["a", "b"]], "ignore": ["b"]})",
           R"({"equivalents": [["ab", "c"]]})",
           R"({"equivalents": [[""]]})",
           R"({"equivalents": [[]]})",
           R"({"equivalents": ["a"]})",
           R"({"equivalents": "a"})",
           R"({"equivalents": [[1]]})",
           R"({"ignore": "x"})",
           R"({"ignore": ["xy"]})",
           R"({"ignore": [null]})",
       }) {
    INFO(bad);
    const Answer found = find(file, R"({"values": [{"text": "EU", "folding": )" + bad + "}]}");
    CHECK(found.status == PDFKIT_E_INVALID_ARGUMENT);
    CHECK(found.json.find(R"("code":"invalid_argument")") != std::string::npos);
    const pdfkit::test::Redaction redacted =
        pdfkit::test::redact(file, R"({"matches": [{"search": "EU", "folding": )" + bad + "}]}");
    CHECK(redacted.match_status == PDFKIT_E_INVALID_ARGUMENT);
    CHECK(check(file, R"({"strings": [{"text": "EU", "folding": )" + bad + "}]}").status ==
          PDFKIT_E_INVALID_ARGUMENT);
  }
}

TEST_CASE("two searches starting on one glyph: the longer keeps it, then the one recorded first") {
  pdfkit::test::TextDocument fixture;
  fixture.page("BT /F1 10 Tf 72 700 Td (Contoso Group report) Tj ET");
  const std::string file = fixture.bytes();
  const auto drawn = [&file](const std::string& matches) {
    const pdfkit::test::Redaction result = pdfkit::test::redact(file, matches);
    INFO(result.matches, result.report);
    REQUIRE(result.match_status == PDFKIT_OK);
    REQUIRE(result.write_status == PDFKIT_OK);
    return spaced(pdfkit::test::page_text(result.pdf));
  };
  const std::string shorter = R"({"search": "Contoso", "placeholder": "[A]"})";
  const std::string longer = R"({"search": "Contoso Group", "placeholder": "[B]"})";
  const std::string again = R"({"search": "Contoso", "placeholder": "[C]"})";
  const auto both = [](const std::string& first, const std::string& second) {
    std::string out = R"({"matches": [)";
    out += first;
    out += ", ";
    out += second;
    out += "]}";
    return out;
  };
  CHECK(drawn(both(shorter, longer)) == "[B] report");
  CHECK(drawn(both(longer, shorter)) == "[B] report");
  CHECK(drawn(both(shorter, again)) == "[A] Group report");
  CHECK(drawn(both(again, shorter)) == "[C] Group report");
  // Across calls, the earlier call's match is recorded first.
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                      &doc, &out) == PDFKIT_OK);
  (void)take(out);
  pdfkit_edit* edit = nullptr;
  REQUIRE(pdfkit_edit_new(doc, &edit, &out) == PDFKIT_OK);
  (void)take(out);
  for (const std::string& match : {again, shorter}) {
    REQUIRE(pdfkit_redact_text(edit, (R"({"matches": [)" + match + "]}").c_str(), &out) ==
            PDFKIT_OK);
    (void)take(out);
  }
  pdfkit_buffer pdf{nullptr, 0, nullptr};
  REQUIRE(pdfkit_write(edit, nullptr, &pdf, &out) == PDFKIT_OK);
  (void)take(out);
  CHECK(spaced(pdfkit::test::page_text(take(pdf))) == "[C] Group report");
  pdfkit_edit_free(edit);
  pdfkit_close(doc);
}
