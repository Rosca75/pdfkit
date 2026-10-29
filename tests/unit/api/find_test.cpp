// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// pdfkit_find through the C ABI (docs/ABI.md, the pdfkit_find section): the
// answer's shape and key order, the fingerprint pdfkit_text reports, the
// pages and context options, the same bytes on a second call, the refusals
// of malformed values, and the rule the function exists for: its
// occurrences are exactly the ones pdfkit_redact_text removes for the same
// search, range for range. A range it reports, sent back as a located match,
// removes that occurrence and keeps the others.

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "redact/support/redaction.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
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

// pdfkit_find over file; a second call on the same handle must give the same
// bytes.
Answer find(const std::string& file, const std::string& values) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                      &doc, &out) == PDFKIT_OK);
  (void)take(out);
  Answer answer;
  answer.status = pdfkit_find(doc, values.c_str(), &out);
  answer.json = take(out);
  const pdfkit_status again = pdfkit_find(doc, values.c_str(), &out);
  CHECK(again == answer.status);
  CHECK(take(out) == answer.json);
  pdfkit_close(doc);
  return answer;
}

std::string text_fingerprint(const std::string& file) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                      &doc, &out) == PDFKIT_OK);
  (void)take(out);
  REQUIRE(pdfkit_text(doc, R"({"blocks": false})", &out) == PDFKIT_OK);
  const JsonValue text = JsonReader::parse(take(out));
  pdfkit_close(doc);
  return text.find("fingerprint")->as_string();
}

using Range = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

// The ranges of every occurrence of value index v in a find answer.
std::vector<Range> find_ranges(const JsonValue& answer, std::size_t v) {
  std::vector<Range> out;
  for (const JsonValue& occurrence :
       answer.find("values")->as_array()[v].find("occurrences")->as_array()) {
    for (const JsonValue& range : occurrence.find("glyphs")->as_array()) {
      out.emplace_back(range.find("page")->as_int64().value_or(-1),
                       range.find("first")->as_int64().value_or(-1),
                       range.find("last")->as_int64().value_or(-1));
    }
  }
  std::ranges::sort(out);
  return out;
}

// The ranges of every occurrence of match index m in a redact_text answer.
std::vector<Range> redact_ranges(const JsonValue& answer, std::size_t m) {
  std::vector<Range> out;
  for (const JsonValue& occurrence :
       answer.find("matches")->as_array()[m].find("occurrences")->as_array()) {
    const auto& glyphs = occurrence.find("glyphs")->as_array();
    out.emplace_back(occurrence.find("page")->as_int64().value_or(-1),
                     glyphs[0].as_int64().value_or(-1), glyphs[1].as_int64().value_or(-1));
  }
  std::ranges::sort(out);
  return out;
}

std::string two_pages() {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "BT /F1 10 Tf 72 700 Td (the member states of the EU agreed on the following) Tj ET");
  fixture.page(
      "BT /F1 14 Tf 72 700 Td (Alpha Bravo) Tj 0 -24 Td (Charlie Delta) Tj 0 -24 Td (EU) Tj "
      "ET BT /F1 14 Tf 74 688 Td (Golf Hotel) Tj 0 -24 Td (India Juliet) Tj 0 -24 Td (Kilo) "
      "Tj ET BT /F1 10 Tf 3 Tr 72 500 Td (hidden EUROPE) Tj ET");
  return fixture.bytes();
}

}  // namespace

TEST_CASE("pdfkit_find answers every value with its occurrences in the contract's shape") {
  const std::string file = two_pages();
  const Answer answer =
      find(file, R"({"values": [{"text": "EU"}, {"text": "Bravo Charlie"}, {"text": "absent"}]})");
  REQUIRE(answer.status == PDFKIT_OK);
  INFO(answer.json);
  // The keys in the contract's order.
  CHECK(answer.json.starts_with(R"({"fingerprint":")"));
  CHECK(answer.json.find(R"({"value":0,"text":"EU","count":)") != std::string::npos);
  CHECK(answer.json.find(R"({"occurrence":0,"page":1,"glyphs":[{"page":1,"first":)") !=
        std::string::npos);
  CHECK(answer.json.find(R"("text":"EU","order":"reading","rectangles":[[)") != std::string::npos);
  CHECK(
      answer.json.find(
          R"("hidden":[],"before":"the member states of the","after":"agreed on the following","glued_before":"","glued_after":""})") !=
      std::string::npos);
  const JsonValue json = JsonReader::parse(answer.json);
  CHECK(json.find("fingerprint")->as_string() == text_fingerprint(file));
  const auto& values = json.find("values")->as_array();
  REQUIRE(values.size() == 3);
  // "EU" on page 1, on page 2, and inside the hidden "EUROPE".
  CHECK(values[0].find("count")->as_int64().value_or(-1) == 3);
  const auto& eu = values[0].find("occurrences")->as_array();
  CHECK(eu[0].find("page")->as_int64().value_or(-1) == 1);
  CHECK(eu[2].find("occurrence")->as_int64().value_or(-1) == 2);
  CHECK(eu[2].find("hidden")->as_array()[0].as_string() == "render_invisible");
  const auto& bravo = values[1].find("occurrences")->as_array();
  REQUIRE(bravo.size() == 1);
  CHECK(bravo[0].find("order")->as_string() == "drawing");
  CHECK(bravo[0].find("glyphs")->as_array().size() == 2);
  CHECK(values[2].find("count")->as_int64().value_or(-1) == 0);
  CHECK(values[2].find("occurrences")->as_array().empty());
}

TEST_CASE("pdfkit_find reads only the pages asked and as many words of context as asked") {
  const std::string file = two_pages();
  const Answer answer = find(file, R"({"values": [{"text": "EU"}], "pages": [2], "context": 1})");
  REQUIRE(answer.status == PDFKIT_OK);
  INFO(answer.json);
  const JsonValue json = JsonReader::parse(answer.json);
  const auto& occurrences = json.find("values")->as_array()[0].find("occurrences")->as_array();
  REQUIRE(occurrences.size() == 2);
  for (const JsonValue& occurrence : occurrences) {
    CHECK(occurrence.find("page")->as_int64().value_or(-1) == 2);
  }
  CHECK(occurrences[0].find("before")->as_string().find(' ') == std::string::npos);
  CHECK(occurrences[0].find("after")->as_string().find(' ') == std::string::npos);
  const Answer folded =
      find(file, R"({"values": [{"text": "eu", "folding": {"case": false}}], "pages": [1]})");
  REQUIRE(folded.status == PDFKIT_OK);
  CHECK(folded.json.find(R"("count":0)") != std::string::npos);
}

TEST_CASE("pdfkit_find refuses malformed values with invalid_argument") {
  const std::string file = two_pages();
  for (const std::string bad : {
           R"({})",
           R"({"values": {}})",
           R"({"values": [{"text": ""}]})",
           R"({"values": [{}]})",
           R"({"values": [{"text": 3}]})",
           R"({"values": [{"text": "a", "other": 1}]})",
           R"({"values": [{"text": "a", "folding": {"accents": true}}]})",
           R"({"values": [{"text": "a", "folding": {"case": "yes"}}]})",
           R"({"values": [{"text": "a"}], "pages": [3]})",
           R"({"values": [{"text": "a"}], "pages": [1, 1]})",
           R"({"values": [{"text": "a"}], "context": 51})",
           R"({"values": [{"text": "a"}], "context": -1})",
           R"({"values": [{"text": "a"}], "extra": true})",
           R"(not json)",
       }) {
    INFO(bad);
    const Answer answer = find(file, bad);
    CHECK(answer.status == PDFKIT_E_INVALID_ARGUMENT);
    CHECK(answer.json.find(R"("code":"invalid_argument")") != std::string::npos);
  }
  pdfkit_buffer out{nullptr, 0, nullptr};
  CHECK(pdfkit_find(nullptr, R"({"values": []})", &out) == PDFKIT_E_INVALID_ARGUMENT);
  pdfkit_buffer_free(&out);
}

TEST_CASE("pdfkit_find's occurrences are the ones pdfkit_redact_text removes, range for range") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "BT /F1 10 Tf 72 700 Td (Quarterly Annual) Tj 0 -12 Td (more text here) Tj ET BT /F1 10 "
      "Tf 320 700 Td (Report follows) Tj 0 -12 Td (second column) Tj ET");
  fixture.page(
      "BT /F1 10 Tf 72 700 Td (T) Tj 4.44 -2.2 Td (E) Tj 4.9 2.2 Td (X) Tj 12 0 Td "
      "(logo and texts) Tj ET");
  const std::string file = fixture.bytes();
  const std::string file2 = two_pages();
  for (const std::string* source : {&file, &file2}) {
    for (const std::string value :
         {"Annual Report", "TeX", "e", "eu", "Bravo Charlie", "text", "o"}) {
      INFO(value);
      const Answer found = find(*source, R"({"values": [{"text": ")" + value + R"(", )" +
                                             std::string(pdfkit::test::every_folding) + "}]}");
      REQUIRE(found.status == PDFKIT_OK);
      const pdfkit::test::Redaction redacted =
          pdfkit::test::redact(*source, pdfkit::test::search(value));
      REQUIRE(redacted.match_status == PDFKIT_OK);
      CHECK(find_ranges(JsonReader::parse(found.json), 0) ==
            redact_ranges(JsonReader::parse(redacted.matches), 0));
    }
  }
}

TEST_CASE("a range pdfkit_find reports, sent back as a located match, removes that occurrence") {
  const std::string file = two_pages();
  // "EU" on page 2 (one range, the reading order) and "Bravo Charlie" (two
  // ranges, the drawing order): each sent back alone leaves every other
  // occurrence of its value.
  for (const auto& [value, chosen, left] :
       {std::tuple<std::string, std::size_t, std::int64_t>{"EU", 1, 2},
        std::tuple<std::string, std::size_t, std::int64_t>{"Bravo Charlie", 0, 0}}) {
    INFO(value);
    const std::string values = R"({"values": [{"text": ")" + value + R"("}]})";
    const Answer found = find(file, values);
    REQUIRE(found.status == PDFKIT_OK);
    const JsonValue answer = JsonReader::parse(found.json);
    const auto& occurrences = answer.find("values")->as_array()[0].find("occurrences")->as_array();
    REQUIRE(chosen < occurrences.size());
    std::string glyphs = "[";
    for (const JsonValue& range : occurrences[chosen].find("glyphs")->as_array()) {
      glyphs += (glyphs.size() == 1 ? "" : ", ") + std::string(R"({"page": )") +
                std::to_string(range.find("page")->as_int64().value_or(-1)) + R"(, "first": )" +
                std::to_string(range.find("first")->as_int64().value_or(-1)) + R"(, "last": )" +
                std::to_string(range.find("last")->as_int64().value_or(-1)) + "}";
    }
    glyphs += ']';
    const pdfkit::test::Redaction redacted = pdfkit::test::redact(
        file, R"({"matches": [{"glyphs": )" + glyphs + R"(, "fingerprint": ")" +
                  answer.find("fingerprint")->as_string() + R"(", "placeholder": ""}]})");
    INFO(redacted.matches, redacted.report);
    REQUIRE(redacted.match_status == PDFKIT_OK);
    REQUIRE(redacted.write_status == PDFKIT_OK);
    const Answer after = find(redacted.pdf, values);
    REQUIRE(after.status == PDFKIT_OK);
    CHECK(JsonReader::parse(after.json)
              .find("values")
              ->as_array()[0]
              .find("count")
              ->as_int64()
              .value_or(-1) == left);
  }
}
