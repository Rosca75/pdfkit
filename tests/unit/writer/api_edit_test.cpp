// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the four symbols of the writer through the C ABI: pdfkit_edit_new,
// pdfkit_select_pages, pdfkit_write and pdfkit_channels, their status codes,
// the JSON error object's three fields for each refusal, the write report's
// shape, and every buffer freed.

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "support/pdf_builder.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;

std::string three_pages() {
  pdfkit::test::PdfBuilder builder;
  builder.begin_document();
  for (int i = 0; i < 3; ++i) {
    (void)builder.add_page("BT ET");
  }
  return builder.build();
}

struct Session {
  pdfkit_document* doc = nullptr;
  pdfkit_edit* edit = nullptr;

  explicit Session(const std::string& file) {
    pdfkit_buffer out{nullptr, 0, nullptr};
    REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                        &doc, &out) == PDFKIT_OK);
    pdfkit_buffer_free(&out);
    REQUIRE(pdfkit_edit_new(doc, &edit, &out) == PDFKIT_OK);
    CHECK(std::string_view(reinterpret_cast<const char*>(out.data), out.size) == "{}");
    pdfkit_buffer_free(&out);
  }
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(Session&&) = delete;
  ~Session() {
    pdfkit_edit_free(edit);
    pdfkit_close(doc);
  }
};

std::string text_of(pdfkit_buffer& out) {
  std::string text(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  CHECK(out.data == nullptr);
  return text;
}

void check_error(const std::string& text, std::string_view code) {
  INFO(text);
  const JsonValue error = JsonReader::parse(text);
  CHECK(error.find("code")->as_string() == code);
  CHECK_FALSE(error.find("failed")->as_string().empty());
  CHECK_FALSE(error.find("expected")->as_string().empty());
  CHECK_FALSE(error.find("fix")->as_string().empty());
}

pdfkit_status select(Session& session, const char* json, std::string& text) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_select_pages(session.edit, json, &out);
  text = text_of(out);
  return status;
}

}  // namespace

TEST_CASE("pdfkit_edit_new refuses a null document and a null out_edit") {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_edit* edit = nullptr;
  CHECK(pdfkit_edit_new(nullptr, &edit, &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(text_of(out), "invalid_argument");
  CHECK(edit == nullptr);
  const Session session(three_pages());
  CHECK(pdfkit_edit_new(session.doc, nullptr, &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(text_of(out), "invalid_argument");
  CHECK(pdfkit_edit_new(session.doc, &edit, nullptr) == PDFKIT_E_INVALID_ARGUMENT);
  pdfkit_edit_free(nullptr);
}

TEST_CASE("pdfkit_select_pages refuses an empty, duplicated or out-of-range list") {
  Session session(three_pages());
  std::string text;
  for (const char* bad :
       {R"({"pages": []})", R"({"pages": [1, 2, 1]})", R"({"pages": [0]})", R"({"pages": [4]})",
        R"({"pages": [-1]})", R"({"pages": [1.5]})", R"({"pages": ["1"]})", R"({"pages": 1})",
        R"({})", R"({"pages": [1], "order": "reverse"})", "[1, 2]", "not json"}) {
    INFO(bad);
    CHECK(select(session, bad, text) == PDFKIT_E_INVALID_ARGUMENT);
    check_error(text, "invalid_argument");
  }
  CHECK(select(session, nullptr, text) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(text, "invalid_argument");
  pdfkit_buffer out{nullptr, 0, nullptr};
  CHECK(pdfkit_select_pages(nullptr, R"({"pages": [1]})", &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(text_of(out), "invalid_argument");

  CHECK(select(session, R"({"pages": [3, 1]})", text) == PDFKIT_OK);
  CHECK(text == R"({"pages":[3,1]})");
}

TEST_CASE("the refusal names the page and the document's page count") {
  Session session(three_pages());
  std::string text;
  REQUIRE(select(session, R"({"pages": [2, 9]})", text) == PDFKIT_E_INVALID_ARGUMENT);
  const JsonValue error = JsonReader::parse(text);
  CHECK(error.find("failed")->as_string().find("page 9") != std::string::npos);
  CHECK(error.find("failed")->as_string().find("3 pages") != std::string::npos);
  CHECK(error.find("expected")->as_string() == "page numbers from 1 to 3");
}

TEST_CASE("pdfkit_write returns the bytes and the report, or an error with out_pdf empty") {
  Session session(three_pages());
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_buffer pdf{nullptr, 0, nullptr};
  CHECK(pdfkit_write(session.edit, R"({"compress": true})", &pdf, &out) ==
        PDFKIT_E_INVALID_ARGUMENT);
  CHECK(pdf.data == nullptr);
  check_error(text_of(out), "invalid_argument");
  CHECK(pdfkit_write(session.edit, nullptr, nullptr, &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(text_of(out), "invalid_argument");
  CHECK(pdfkit_write(nullptr, nullptr, &pdf, &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(text_of(out), "invalid_argument");

  std::string text;
  REQUIRE(select(session, R"({"pages": [2, 3]})", text) == PDFKIT_OK);
  REQUIRE(pdfkit_write(session.edit, "{}", &pdf, &out) == PDFKIT_OK);
  const std::string bytes(reinterpret_cast<const char*>(pdf.data), pdf.size);
  pdfkit_buffer_free(&pdf);
  CHECK(bytes.starts_with("%PDF-1.7"));
  const std::string report_text = text_of(out);
  INFO(report_text);
  const JsonValue report = JsonReader::parse(report_text);
  std::string keys;
  for (const auto& member : report.as_object()) {
    keys += member.first + " ";
  }
  CHECK(keys ==
        "page_count pages bytes sha256 objects channels pending_redactions missing_references "
        "unpruned_resources rectangles images self_check warnings ");
  CHECK(report.find("page_count")->as_int64() == 2);
  CHECK(report.find("pages")->as_array().size() == 2);
  CHECK(report.find("pages")->as_array()[0].as_int64() == 2);
  CHECK(report.find("bytes")->as_int64() == static_cast<std::int64_t>(bytes.size()));
  CHECK(report.find("channels")->as_array().size() == 24);
  CHECK(report.find("rectangles")->as_array().empty());
  const JsonValue* check = report.find("self_check");
  CHECK(check->find("status")->as_string() == "not_run");
  CHECK(check->find("reason")->as_string() == "no text redaction in the edit plan");
}

TEST_CASE("pdfkit_channels reports every row and refuses options") {
  const Session session(three_pages());
  pdfkit_buffer out{nullptr, 0, nullptr};
  CHECK(pdfkit_channels(session.doc, R"({"rows": 1})", &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(text_of(out), "invalid_argument");
  CHECK(pdfkit_channels(nullptr, nullptr, &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(text_of(out), "invalid_argument");
  REQUIRE(pdfkit_channels(session.doc, "{}", &out) == PDFKIT_OK);
  const JsonValue report = JsonReader::parse(text_of(out));
  const auto& rows = report.find("channels")->as_array();
  REQUIRE(rows.size() == 24);
  for (const JsonValue& row : rows) {
    CHECK(row.find("present")->as_bool() == false);
    CHECK(row.find("count")->as_int64() == 0);
    CHECK_FALSE(row.find("name")->as_string().empty());
  }
  CHECK(report.find("present")->as_array().empty());
  CHECK(report.find("pending_redactions")->as_int64() == 0);
  CHECK(report.find("warnings")->as_array().empty());
}
