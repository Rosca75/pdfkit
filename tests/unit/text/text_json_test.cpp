// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of pdfkit_text and text::TextJson through the C ABI: the JSON of a
// two-page fixture against the expected string written here, twice in one
// process with identical bytes; the glyph list's fields; the options and
// their refusals (a page out of range, a page twice, an unknown option).

#include "text/text_json.hpp"

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>
#include <string_view>

namespace {

using pdfkit::platform::JsonValue;
using pdfkit::test::TextDocument;

class Opened {
 public:
  explicit Opened(const std::string& bytes) {
    pdfkit_buffer out{nullptr, 0, nullptr};
    status_ = pdfkit_open(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(),
                          nullptr, &document_, &out);
    pdfkit_buffer_free(&out);
  }
  Opened(const Opened&) = delete;
  Opened& operator=(const Opened&) = delete;
  Opened(Opened&&) = delete;
  Opened& operator=(Opened&&) = delete;
  ~Opened() { pdfkit_close(document_); }

  [[nodiscard]] pdfkit_status text(const char* options, std::string& result) {
    pdfkit_buffer out{nullptr, 0, nullptr};
    const pdfkit_status status = pdfkit_text(document_, options, &out);
    result.assign(reinterpret_cast<const char*>(out.data), out.size);
    pdfkit_buffer_free(&out);
    return status;
  }
  [[nodiscard]] pdfkit_status opened() const { return status_; }

 private:
  pdfkit_document* document_ = nullptr;
  pdfkit_status status_ = PDFKIT_E_INTERNAL;
};

std::string two_pages() {
  TextDocument document;
  document.page("BT /F1 10 Tf 72 700 Td (Hello World) Tj ET");
  document.page("BT /F1 10 Tf 72 700 Td (Page two) Tj ET BT 3 Tr /F1 10 Tf 72 600 Td (Hid) Tj ET");
  return document.bytes();
}

constexpr std::string_view expected =
    R"({"pages":[{"page":1,"text":"Hello World","structure_order":false,"blocks":[{"box":[72,697.93,123.67,707.18],"glyphs":[0,11],"lines":[{"box":[72,697.93,123.67,707.18],"glyphs":[0,11],"direction":"ltr","angle":0,"words":[{"text":"Hello","box":[72,697.93,94.78,707.18],"glyphs":[0,5],"hyphenated":false,"hidden":[]},{"text":"World","box":[97.56,697.93,123.67,707.18],"glyphs":[6,11],"hyphenated":false,"hidden":[]}]}]}],"hidden_blocks":[],"unreadable":{"count":0,"fonts":[]},"hidden":{"count":0,"classes":{"render_invisible":0,"white_fill":0,"clipped":0,"off_media":0,"off_crop":0,"optional_content_off":0,"duplicate":0,"covered":0}},"rungs":{"to_unicode":0,"glyph_name":11,"font_program":0,"actual_text":0,"unreadable":0},"warnings":[]},{"page":2,"text":"Page two\n\nHid","structure_order":false,"blocks":[{"box":[72,697.93,113.69,707.18],"glyphs":[0,8],"lines":[{"box":[72,697.93,113.69,707.18],"glyphs":[0,8],"direction":"ltr","angle":0,"words":[{"text":"Page","box":[72,697.93,95.35,707.18],"glyphs":[0,4],"hyphenated":false,"hidden":[]},{"text":"two","box":[98.13,697.93,113.69,707.18],"glyphs":[5,8],"hyphenated":false,"hidden":[]}]}]}],"hidden_blocks":[{"box":[72,597.93,87,607.18],"glyphs":[8,11],"lines":[{"box":[72,597.93,87,607.18],"glyphs":[8,11],"direction":"ltr","angle":0,"words":[{"text":"Hid","box":[72,597.93,87,607.18],"glyphs":[8,11],"hyphenated":false,"hidden":["render_invisible"]}]}]}],"unreadable":{"count":0,"fonts":[]},"hidden":{"count":3,"classes":{"render_invisible":3,"white_fill":0,"clipped":0,"off_media":0,"off_crop":0,"optional_content_off":0,"duplicate":0,"covered":0}},"rungs":{"to_unicode":0,"glyph_name":11,"font_program":0,"actual_text":0,"unreadable":0},"warnings":[{"code":"hidden_text","message":"page 2 holds 3 glyphs a viewer does not show (render_invisible 3); they are extracted after the visible text","page":2}]}]})";

}  // namespace

TEST_CASE("the text JSON of a two-page fixture, twice, byte for byte") {
  Opened document(two_pages());
  REQUIRE(document.opened() == PDFKIT_OK);
  std::string first;
  std::string second;
  REQUIRE(document.text(nullptr, first) == PDFKIT_OK);
  REQUIRE(document.text("{}", second) == PDFKIT_OK);
  CHECK(first == expected);
  CHECK(first == second);
}

TEST_CASE("the glyph list carries every field of the glyph record") {
  Opened document(two_pages());
  std::string text;
  REQUIRE(document.text(R"({"pages": [2], "glyphs": true, "reserialisation": true})", text) ==
          PDFKIT_OK);
  const JsonValue result = pdfkit::platform::JsonReader::parse(text);
  const JsonValue& page = result.find("pages")->as_array().front();
  CHECK(page.find("page")->as_int64() == 2);
  const JsonValue::Array& glyphs = page.find("glyphs")->as_array();
  REQUIRE(glyphs.size() == 11U);
  const JsonValue& hid = glyphs[8];
  for (const char* key : {"draw",
                          "stream",
                          "operator",
                          "operand",
                          "bytes",
                          "hex",
                          "font",
                          "code",
                          "code_length",
                          "text",
                          "rung",
                          "decomposed",
                          "width",
                          "adjustment",
                          "advance",
                          "vertical",
                          "text_matrix",
                          "ctm",
                          "char_spacing",
                          "word_spacing",
                          "horizontal_scaling",
                          "leading",
                          "rise",
                          "font_size",
                          "render_mode",
                          "origin",
                          "advance_end",
                          "box",
                          "size",
                          "angle",
                          "fill",
                          "marked_content",
                          "actual_text",
                          "optional_content",
                          "clip",
                          "hidden",
                          "duplicate_of",
                          "duplicates"}) {
    INFO(key);
    CHECK(hid.find(key) != nullptr);
  }
  CHECK(hid.find("text")->as_string() == "H");
  CHECK(hid.find("render_mode")->as_int64() == 3);
  CHECK(hid.find("hidden")->as_array().front().as_string() == "render_invisible");
  const JsonValue& check = *page.find("reserialisation");
  CHECK(check.find("identical")->as_int64() == check.find("streams")->as_int64());
  CHECK(check.find("failed")->as_int64() == 0);
}

TEST_CASE("pdfkit_text refuses a page out of range, a page twice and an unknown option") {
  Opened document(two_pages());
  std::string text;
  CHECK(document.text(R"({"pages": [3]})", text) == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(text.find("page 3 of a document of 2 pages") != std::string::npos);
  CHECK(document.text(R"({"pages": [1, 1]})", text) == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(document.text(R"({"pages": [0]})", text) == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(document.text(R"({"glyph": true})", text) == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(document.text(R"({"glyphs": 1})", text) == PDFKIT_E_INVALID_ARGUMENT);
  CHECK(document.text(R"({"pages": [2], "blocks": false})", text) == PDFKIT_OK);
  CHECK(text.find("\"blocks\"") == std::string::npos);
}
