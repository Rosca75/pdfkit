// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Occurrences in marked-content strings and pointing at one (docs/ABI.md,
// the pdfkit_find and pdfkit_redact_text sections): pdfkit_find reports a
// value in the strings of every BDC and DP property list (inline or named
// through /Properties, an /ActualText that a glyph reads and an /OC list
// left out) with the place each lies; pdfkit_redact_text takes one entry
// back as a string match and replaces exactly that range at that place: an
// inline list in its operator, a named list where it lives when no other
// operator names it and inline in its operator otherwise, a list in a form
// drawn twice in a copy of the form for that draw; a search of the same list
// combines with it; every refusal names what the document does not hold;
// and the self-check passes on every case.

#include "pdfkit/pdfkit.h"

#include "platform/byte_span.hpp"
#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "platform/json_writer.hpp"
#include "redact/self_check.hpp"
#include "redact/support/redaction.hpp"
#include "syntax/limits.hpp"
#include "text/support/text_document.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;

std::string take(pdfkit_buffer& buffer) {
  std::string text(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  return text;
}

// pdfkit_find's answer for values over file.
JsonValue find(const std::string& file, const std::string& values) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                      &doc, &out) == PDFKIT_OK);
  (void)take(out);
  const pdfkit_status status = pdfkit_find(doc, values.c_str(), &out);
  const std::string json = take(out);
  pdfkit_close(doc);
  INFO(json);
  REQUIRE(status == PDFKIT_OK);
  return JsonReader::parse(json);
}

// The strings entries of the first value.
JsonValue::Array strings_of(const JsonValue& answer) {
  return answer.find("values")->as_array()[0].find("strings")->as_array();
}

JsonValue::Array find_strings(const std::string& file, const std::string& text) {
  const JsonValue answer = find(file, R"({"values": [{"text": ")" + text + R"("}]})");
  REQUIRE(answer.find("values")->as_array()[0].find("strings") != nullptr);
  return strings_of(answer);
}

std::string fingerprint_of(const std::string& file) {
  return find(file, R"({"values": [{"text": "x"}]})").find("fingerprint")->as_string();
}

void write_value(pdfkit::platform::JsonWriter& writer, const JsonValue& value) {
  switch (value.kind()) {
    case JsonValue::Kind::object:
      writer.begin_object();
      for (const auto& [name, member] : value.as_object()) {
        writer.key(name);
        write_value(writer, member);
      }
      writer.end_object();
      break;
    case JsonValue::Kind::array:
      writer.begin_array();
      for (const JsonValue& item : value.as_array()) {
        write_value(writer, item);
      }
      writer.end_array();
      break;
    case JsonValue::Kind::string:
      writer.string(value.as_string());
      break;
    case JsonValue::Kind::number:
      writer.integer(value.as_int64().value_or(-1));
      break;
    case JsonValue::Kind::boolean:
      writer.boolean(value.as_bool());
      break;
    default:
      writer.null();
      break;
  }
}

std::string json_of(const JsonValue& value) {
  pdfkit::platform::JsonWriter writer;
  write_value(writer, value);
  return writer.take();
}

// A string match of entry (a find entry, written back whole, or any object).
std::string string_match(const std::string& entry, const std::string& fingerprint,
                         const std::string& placeholder = "[X]") {
  return R"({"string": )" + entry + R"(, "fingerprint": ")" + fingerprint +
         R"(", "placeholder": ")" + placeholder + R"("})";
}

std::string matches(const std::string& match) {
  return R"({"matches": [)" + match + "]}";
}

// The /Alt values of the marked-content spans of page number of output, in
// the order met.
std::vector<std::string> alts(const std::string& output, std::uint32_t number = 1) {
  const std::vector<pdfkit::text::PageText> pages = pdfkit::test::read_pages(output);
  REQUIRE(number <= pages.size());
  std::vector<std::string> out;
  for (const pdfkit::text::MarkedSpan& span : pages[number - 1].content.spans) {
    if (span.alt) {
      out.push_back(*span.alt);
    }
  }
  return out;
}

void require_written(const pdfkit::test::Redaction& result) {
  INFO(result.matches);
  REQUIRE(result.match_status == PDFKIT_OK);
  INFO(result.report);
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(result.report.find(R"("status":"passed")") != std::string::npos);
}

// Every kind of list on one page: an inline /Alt and /E, a direct and an
// indirect named list, an artifact's /Contents, an /ActualText a glyph
// reads, one over no glyph, and an /OC list.
std::string every_list(std::uint32_t& indirect) {
  pdfkit::test::TextDocument fixture;
  indirect = fixture.add("<< /Alt (Indirect Smith) >>");
  fixture.page(
      "/Figure << /Alt (Photo of Smith) >> BDC BT /F1 10 Tf 72 700 Td (Photo) Tj ET EMC "
      "/Span << /E (Smithson) >> BDC BT /F1 10 Tf 72 680 Td (Mister) Tj ET EMC "
      "/Span /P1 BDC BT /F1 10 Tf 72 660 Td (Named) Tj ET EMC "
      "/Span /P2 BDC BT /F1 10 Tf 72 640 Td (Indirect) Tj ET EMC "
      "/Artifact << /Contents (Footer Smith) >> BDC BT /F1 10 Tf 72 620 Td (Footer) Tj ET EMC "
      "/Span << /ActualText (Smith) >> BDC BT /F1 10 Tf 72 600 Td (Sm) Tj ET EMC "
      "/Span << /ActualText (Ghost Smith) >> BDC EMC "
      "/OC << /Type /OCG /Name (Smith layer) >> BDC BT /F1 10 Tf 72 580 Td (Layer) Tj ET EMC",
      "/Properties << /P1 << /Alt (Named Smith) >> /P2 " + std::to_string(indirect) + " 0 R >>");
  return fixture.bytes();
}

std::string text_of(const JsonValue& entry, const char* key) {
  const JsonValue* value = entry.find(key);
  REQUIRE(value != nullptr);
  return value->kind() == JsonValue::Kind::null     ? std::string("null")
         : value->kind() == JsonValue::Kind::string ? value->as_string()
                                                    : json_of(*value);
}

}  // namespace

TEST_CASE("pdfkit_find reports a value in the strings of marked-content property lists") {
  std::uint32_t indirect = 0;
  const std::string file = every_list(indirect);
  const JsonValue answer = find(file, R"({"values": [{"text": "Smith"}]})");
  const JsonValue& value = answer.find("values")->as_array()[0];
  INFO(json_of(answer));
  // The count stays the glyphs': the /ActualText a glyph reads.
  CHECK(value.find("count")->as_int64() == 1);
  CHECK(value.find("occurrences")->as_array().size() == 1);
  // strings comes after occurrences.
  REQUIRE(value.as_object().size() == 5);
  CHECK(value.as_object()[3].first == "occurrences");
  CHECK(value.as_object()[4].first == "strings");
  const JsonValue::Array strings = value.find("strings")->as_array();
  REQUIRE(strings.size() == 6);

  // The keys in the order the contract writes them.
  const std::vector<std::string> keys{"occurrence", "page",         "list",       "surface",
                                      "path",       "stream",       "operator",   "properties",
                                      "object",     "string",       "first",      "last",
                                      "text",       "glued_before", "glued_after"};
  for (const JsonValue& entry : strings) {
    REQUIRE(entry.as_object().size() == keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
      CHECK(entry.as_object()[i].first == keys[i]);
    }
  }
  const JsonValue& alt = strings[0];
  CHECK(alt.find("occurrence")->as_int64() == 0);
  CHECK(alt.find("page")->as_int64() == 1);
  CHECK(alt.find("list")->as_int64() == 0);
  CHECK(text_of(alt, "surface") == "alt");
  CHECK(text_of(alt, "path") == R"(["Alt"])");
  CHECK(alt.find("stream")->as_int64() == 0);
  CHECK(alt.find("operator")->as_int64() == 0);
  CHECK(text_of(alt, "properties") == "null");
  CHECK(text_of(alt, "object") == "null");
  CHECK(text_of(alt, "string") == "Photo of Smith");
  CHECK(alt.find("first")->as_int64() == 9);
  CHECK(alt.find("last")->as_int64() == 14);
  CHECK(text_of(alt, "text") == "Smith");
  CHECK(text_of(alt, "glued_before").empty());
  CHECK(text_of(alt, "glued_after").empty());

  const JsonValue& expansion = strings[1];
  CHECK(expansion.find("list")->as_int64() == 1);
  CHECK(text_of(expansion, "surface") == "expansion");
  CHECK(text_of(expansion, "path") == R"(["E"])");
  CHECK(expansion.find("operator")->as_int64() == 7);
  CHECK(expansion.find("first")->as_int64() == 0);
  CHECK(expansion.find("last")->as_int64() == 5);
  CHECK(text_of(expansion, "glued_before").empty());
  CHECK(text_of(expansion, "glued_after") == "son");

  const JsonValue& direct = strings[2];
  CHECK(direct.find("list")->as_int64() == 2);
  CHECK(text_of(direct, "properties") == "P1");
  CHECK(text_of(direct, "object") == "null");
  CHECK(text_of(direct, "string") == "Named Smith");

  const JsonValue& named = strings[3];
  CHECK(named.find("list")->as_int64() == 3);
  CHECK(text_of(named, "properties") == "P2");
  CHECK(named.find("object")->as_int64() == indirect);
  CHECK(text_of(named, "surface") == "alt");

  const JsonValue& artifact = strings[4];
  CHECK(artifact.find("list")->as_int64() == 4);
  CHECK(text_of(artifact, "surface") == "property");
  CHECK(text_of(artifact, "path") == R"(["Contents"])");
  CHECK(text_of(artifact, "string") == "Footer Smith");

  // The /ActualText the glyphs read (list 5) is in occurrences already; the
  // one over no glyph is reported; the /OC list is no list.
  const JsonValue& ghost = strings[5];
  CHECK(ghost.find("list")->as_int64() == 6);
  CHECK(text_of(ghost, "surface") == "actual_text");
  CHECK(text_of(ghost, "string") == "Ghost Smith");
  for (std::size_t i = 0; i < strings.size(); ++i) {
    CHECK(strings[i].find("occurrence")->as_int64() == static_cast<std::int64_t>(i));
  }
}

TEST_CASE("a string match replaces exactly its range at its place") {
  std::uint32_t indirect = 0;
  const std::string file = every_list(indirect);
  const JsonValue::Array before = find_strings(file, "Smith");
  REQUIRE(before.size() == 6);
  const std::string fingerprint = fingerprint_of(file);
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, matches(string_match(json_of(before[0]), fingerprint)));
  require_written(result);

  const JsonValue answer = JsonReader::parse(result.matches);
  const JsonValue& match = answer.find("matches")->as_array()[0];
  CHECK(text_of(match, "kind") == "string");
  CHECK(text_of(match, "placeholder") == "[X]");
  CHECK(match.find("count")->as_int64() == 1);
  REQUIRE(match.find("occurrences")->as_array().size() == 1);
  const JsonValue& found = match.find("occurrences")->as_array()[0];
  CHECK(json_of(found) ==
        R"({"page":1,"list":0,"path":["Alt"],"first":9,"last":14,"text":"Smith"})");

  // The edited string reads with the placeholder; every other string keeps
  // the value.
  const std::vector<std::string> written = alts(result.pdf);
  REQUIRE_FALSE(written.empty());
  CHECK(written[0] == "Photo of [X]");
  const JsonValue::Array after = find_strings(result.pdf, "Smith");
  CHECK(after.size() == 5);
  for (const JsonValue& entry : after) {
    CHECK(text_of(entry, "string") != "Photo of Smith");
  }
  CHECK(find_strings(result.pdf, "Smithson").size() == 1);
  CHECK(find_strings(result.pdf, "Footer Smith").size() == 1);
}

TEST_CASE("a named list two operators name is edited inline in the operator the match names") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "/Figure /P1 BDC BT /F1 10 Tf 72 700 Td (One) Tj ET EMC "
      "/Figure /P1 BDC BT /F1 10 Tf 72 680 Td (Two) Tj ET EMC",
      "/Properties << /P1 << /Alt (Photo of Smith) /MCID 3 >> >>");
  const std::string file = fixture.bytes();
  const JsonValue::Array strings = find_strings(file, "Smith");
  REQUIRE(strings.size() == 2);
  CHECK(text_of(strings[1], "properties") == "P1");
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, matches(string_match(json_of(strings[0]), fingerprint_of(file))));
  require_written(result);
  CHECK(alts(result.pdf) == std::vector<std::string>{"Photo of [X]", "Photo of Smith"});
  // The other operator still names the original.
  CHECK(pdfkit::test::contains_marker(result.pdf, "/P1 BDC"));
}

TEST_CASE("a named list one operator names is edited where it lives") {
  pdfkit::test::TextDocument fixture;
  std::string resources;
  SUBCASE("an indirect list") {
    const std::uint32_t list = fixture.add("<< /Alt (Photo of Smith) >>");
    resources = "/Properties << /P1 " + std::to_string(list) + " 0 R >>";
  }
  SUBCASE("a direct entry of /Properties") {
    resources = "/Properties << /P1 << /Alt (Photo of Smith) >> >>";
  }
  fixture.page("/Figure /P1 BDC BT /F1 10 Tf 72 700 Td (Photo) Tj ET EMC", resources);
  const std::string file = fixture.bytes();
  const JsonValue::Array strings = find_strings(file, "Smith");
  REQUIRE(strings.size() == 1);
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, matches(string_match(json_of(strings[0]), fingerprint_of(file))));
  require_written(result);
  CHECK(alts(result.pdf) == std::vector<std::string>{"Photo of [X]"});
  // The operator still names the list, and no copy of the original is left.
  CHECK(pdfkit::test::contains_marker(result.pdf, "/P1 BDC"));
  pdfkit_buffer out{nullptr, 0, nullptr};
  REQUIRE(pdfkit_check(reinterpret_cast<const std::uint8_t*>(result.pdf.data()), result.pdf.size(),
                       R"({"strings": [{"text": "Smith"}]})", &out) == PDFKIT_OK);
  const std::string check = take(out);
  INFO(check);
  CHECK(check.find(R"("surface":"string")") == std::string::npos);
  CHECK(check.find(R"("surface":"alt")") == std::string::npos);
  CHECK(check.find(R"("count":0)") != std::string::npos);
}

TEST_CASE("a list in a form drawn on two pages is edited for the draw the match names") {
  pdfkit::test::TextDocument fixture;
  const std::string font = std::to_string(fixture.helvetica());
  const std::uint32_t form = fixture.add_stream(
      "/Type /XObject /Subtype /Form /BBox [0 0 600 100] /Resources << /Font << /F1 " + font +
          " 0 R >> >> ",
      "/Figure << /Alt (Photo of Smith) >> BDC BT /F1 10 Tf 10 50 Td (Photo) Tj ET EMC");
  for (int page = 1; page <= 2; ++page) {
    fixture.page("q 1 0 0 1 72 40 cm /Fm1 Do Q",
                 "/XObject << /Fm1 " + std::to_string(form) + " 0 R >>");
  }
  const std::string file = fixture.bytes();
  const JsonValue answer = find(file, R"({"values": [{"text": "Smith"}], "pages": [1]})");
  const JsonValue::Array strings = strings_of(answer);
  REQUIRE(strings.size() == 1);
  CHECK(strings[0].find("stream")->as_int64() == form);
  CHECK(strings[0].find("operator")->as_int64() == 0);
  const pdfkit::test::Redaction result = pdfkit::test::redact(
      file, matches(string_match(json_of(strings[0]), answer.find("fingerprint")->as_string())));
  require_written(result);
  CHECK(alts(result.pdf, 1) == std::vector<std::string>{"Photo of [X]"});
  CHECK(alts(result.pdf, 2) == std::vector<std::string>{"Photo of Smith"});
}

TEST_CASE("a search and a string match on one list combine") {
  pdfkit::test::TextDocument fixture;
  std::string content =
      "/Figure << /Alt (Photo of Smith and Jones) >> BDC BT /F1 10 Tf 72 700 Td (Photo) Tj ET "
      "EMC";
  std::string resources;
  SUBCASE("in the page's content") {}
  SUBCASE("in a form drawn once") {
    const std::string font = std::to_string(fixture.helvetica());
    const std::uint32_t form = fixture.add_stream(
        "/Type /XObject /Subtype /Form /BBox [0 0 600 800] /Resources << /Font << /F1 " + font +
            " 0 R >> >> ",
        content);
    content = "/Fm1 Do";
    resources = "/XObject << /Fm1 " + std::to_string(form) + " 0 R >>";
  }
  SUBCASE("in a named list") {
    content = "/Figure /P1 BDC BT /F1 10 Tf 72 700 Td (Photo) Tj ET EMC";
    resources = "/Properties << /P1 << /Alt (Photo of Smith and Jones) >> >>";
  }
  fixture.page(content, resources);
  const std::string file = fixture.bytes();
  const JsonValue::Array strings = find_strings(file, "Smith");
  REQUIRE(strings.size() == 1);
  const pdfkit::test::Redaction result = pdfkit::test::redact(
      file, R"({"matches": [{"search": "Jones", "placeholder": "[J]"}, )" +
                string_match(json_of(strings[0]), fingerprint_of(file), "[S]") + "]}");
  require_written(result);
  CHECK(alts(result.pdf) == std::vector<std::string>{"Photo of [S] and [J]"});
}

TEST_CASE("string matches on one string follow the overlap rule and identical ones are one") {
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "/Figure << /Alt (Smith met Smithson) >> BDC BT /F1 10 Tf 72 700 Td (Photo) Tj ET EMC");
  const std::string file = fixture.bytes();
  const std::string fingerprint = fingerprint_of(file);
  const std::string base = R"({"page": 1, "list": 0, "path": ["Alt"], )";
  const std::string first = base + R"("first": 0, "last": 5})";
  const std::string inner = base + R"("first": 2, "last": 9})";
  const std::string last = base + R"("first": 10, "last": 15})";
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, matches(string_match(first, fingerprint, "[A]") + ", " +
                                         string_match(inner, fingerprint, "[B]") + ", " +
                                         string_match(last, fingerprint, "[C]") + ", " +
                                         string_match(last, fingerprint, "[C]")));
  require_written(result);
  // [0, 5) keeps its range, [2, 9) draws over what is left of it.
  CHECK(alts(result.pdf) == std::vector<std::string>{"[A][B] [C]son"});
}

TEST_CASE("a string match is refused when it names what the document does not hold") {
  std::uint32_t indirect = 0;
  const std::string file = every_list(indirect);
  const std::string fingerprint = fingerprint_of(file);
  const JsonValue::Array strings = find_strings(file, "Smith");
  REQUIRE(strings.size() == 6);
  const std::string entry = json_of(strings[0]);
  const auto replaced = [&entry](const std::string& from, const std::string& to) {
    std::string out = entry;
    const std::size_t at = out.find(from);
    REQUIRE(at != std::string::npos);
    out.replace(at, from.size(), to);
    return out;
  };
  const auto refused = [&file](const std::string& json) {
    const pdfkit::test::Redaction result = pdfkit::test::redact(file, json);
    INFO(json);
    INFO(result.matches);
    CHECK(result.match_status == PDFKIT_E_INVALID_ARGUMENT);
    CHECK(result.matches.find(R"("code":"invalid_argument")") != std::string::npos);
  };
  // The entry as found is taken.
  const pdfkit::test::Redaction taken =
      pdfkit::test::redact(file, matches(string_match(entry, fingerprint)));
  require_written(taken);

  refused(matches(string_match(entry, std::string(64, '0'))));
  refused(matches(string_match(replaced(R"("page":1)", R"("page":2)"), fingerprint)));
  refused(matches(string_match(replaced(R"("list":0)", R"("list":9)"), fingerprint)));
  refused(matches(string_match(replaced(R"(["Alt"])", R"(["Nope"])"), fingerprint)));
  refused(matches(string_match(replaced(R"(["Alt"])", R"(["Alt", 0])"), fingerprint)));
  refused(matches(string_match(replaced(R"("first":9)", R"("first":14)"), fingerprint)));
  refused(matches(string_match(replaced(R"("last":14)", R"("last":15)"), fingerprint)));
  refused(matches(string_match(replaced(R"("text":"Smith")", R"("text":"Jones")"), fingerprint)));
  refused(matches(
      string_match(replaced(R"("string":"Photo of Smith")", R"("string":"Photo")"), fingerprint)));
  refused(matches(
      string_match(replaced(R"("surface":"alt")", R"("surface":"property")"), fingerprint)));
  refused(matches(string_match(replaced(R"("stream":0)", R"("stream":4)"), fingerprint)));
  refused(matches(string_match(replaced(R"("operator":0)", R"("operator":1)"), fingerprint)));
  refused(
      matches(string_match(replaced(R"("properties":null)", R"("properties":"P1")"), fingerprint)));
  refused(matches(string_match(replaced(R"("object":null)", R"("object":7)"), fingerprint)));
  refused(matches(
      string_match(replaced(R"("glued_after":"")", R"("glued_after":"son")"), fingerprint)));
  refused(matches(string_match(entry, fingerprint, "[x]")));
  refused(matches(string_match(replaced(R"("page":1)", R"("page":1, "colour":1)"), fingerprint)));
  // The /ActualText a glyph reads is no string a match can name.
  refused(matches(string_match(
      R"({"page": 1, "list": 5, "path": ["ActualText"], "first": 0, "last": 5})", fingerprint)));
  // A match with a string and a search, or a folding beside the string.
  refused(matches(R"({"search": "Smith", "string": )" + entry + R"(, "fingerprint": ")" +
                  fingerprint + R"("})"));
  refused(matches(R"({"string": )" + entry + R"(, "fingerprint": ")" + fingerprint +
                  R"(", "folding": {"case": true}})"));
}

TEST_CASE("the self-check verifies a string match on its page") {
  // A list two operators name, one edited inline: the output holds the text
  // once fewer than the input, which is what the check asks.
  pdfkit::test::TextDocument fixture;
  fixture.page(
      "/Figure /P1 BDC BT /F1 10 Tf 72 700 Td (One) Tj ET EMC "
      "/Figure /P1 BDC BT /F1 10 Tf 72 680 Td (Two) Tj ET EMC "
      "/Figure << /Alt (Smith again) >> BDC BT /F1 10 Tf 72 660 Td (Three) Tj ET EMC",
      "/Properties << /P1 << /Alt (Photo of Smith) >> >>");
  const std::string file = fixture.bytes();
  const JsonValue::Array strings = find_strings(file, "Smith");
  REQUIRE(strings.size() == 3);
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, matches(string_match(json_of(strings[1]), fingerprint_of(file))));
  require_written(result);
  CHECK(result.report.find(R"("strings":1)") != std::string::npos);
  CHECK(alts(result.pdf) ==
        std::vector<std::string>{"Photo of Smith", "Photo of [X]", "Smith again"});
}

TEST_CASE("the self-check refuses a page whose strings hold the text as often as before") {
  std::uint32_t indirect = 0;
  const std::string file = every_list(indirect);
  using pdfkit::redact::SelfCheck;
  // Six strings of the page hold "Smith".
  const auto run = [&file](std::size_t before) {
    const std::vector<SelfCheck::StringCheck> checks{{"Smith", 1, "alt", before, 1}};
    return SelfCheck::run(pdfkit::platform::as_bytes(file), pdfkit::syntax::Limits{},
                          SelfCheck::Needles{{}, {}, {}, {}, checks}, std::vector<std::string>{});
  };
  const SelfCheck::Result unchanged = run(6);
  REQUIRE(unchanged.survivors.size() == 1);
  CHECK(unchanged.survivors[0].surface == "alt");
  CHECK(unchanged.survivors[0].page == 1U);
  CHECK(unchanged.strings == 1);
  CHECK(run(7).survivors.empty());
}

TEST_CASE("whether a named list is edited where it lives is decided over the written pages") {
  pdfkit::test::TextDocument fixture;
  const std::uint32_t list = fixture.add("<< /Alt (Photo of Smith) >>");
  const std::string resources = "/Properties << /P1 " + std::to_string(list) + " 0 R >>";
  for (int page = 1; page <= 2; ++page) {
    fixture.page("/Figure /P1 BDC BT /F1 10 Tf 72 700 Td (Photo) Tj ET EMC", resources);
  }
  const std::string file = fixture.bytes();
  const JsonValue answer = find(file, R"({"values": [{"text": "Smith"}], "pages": [1]})");
  const JsonValue::Array strings = strings_of(answer);
  REQUIRE(strings.size() == 1);
  const std::string match =
      matches(string_match(json_of(strings[0]), answer.find("fingerprint")->as_string()));
  // Both pages written: page 2 still names the original.
  const pdfkit::test::Redaction both = pdfkit::test::redact(file, match);
  require_written(both);
  CHECK(alts(both.pdf, 1) == std::vector<std::string>{"Photo of [X]"});
  CHECK(alts(both.pdf, 2) == std::vector<std::string>{"Photo of Smith"});
  // Page 1 alone: the list is edited where it lives, no copy is left.
  const pdfkit::test::Redaction alone = pdfkit::test::redact(file, match, {1});
  require_written(alone);
  CHECK(alts(alone.pdf) == std::vector<std::string>{"Photo of [X]"});
  CHECK(pdfkit::test::contains_marker(alone.pdf, "/P1 BDC"));
  CHECK_FALSE(pdfkit::test::contains_marker(alone.pdf, "Smith"));
}

TEST_CASE("a search removes a string a list names by reference, as find reports it") {
  pdfkit::test::TextDocument fixture;
  const std::uint32_t label = fixture.add("(Label of Smith)");
  // A content stream holds no reference, so the list is named.
  fixture.page("BT /F1 10 Tf 72 700 Td /Span /P1 BDC (shown) Tj EMC ET",
               "/Properties << /P1 << /Label " + std::to_string(label) + " 0 R >> >>");
  const std::string file = fixture.bytes();
  REQUIRE(find_strings(file, "Smith").size() == 1);
  const pdfkit::test::Redaction result =
      pdfkit::test::redact(file, R"({"matches": [{"search": "Smith", "placeholder": "[X]"}]})");
  INFO(result.matches, result.report);
  REQUIRE(result.match_status == PDFKIT_OK);
  REQUIRE(result.write_status == PDFKIT_OK);
  CHECK(find_strings(result.pdf, "Smith").empty());
  CHECK(find_strings(result.pdf, "[X]").size() == 1);
}
