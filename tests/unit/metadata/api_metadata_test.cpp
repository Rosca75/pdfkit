// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of pdfkit_metadata and pdfkit_set_metadata through the C ABI: the
// JSON of each, both statuses, and each refusal's code with its three fields
// (a field outside the list, a value of the wrong kind, an option metadata
// does not take).

#include "pdfkit/pdfkit.h"

#include "metadata/support/metadata_fixture.hpp"
#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;
using pdfkit::test::MetadataFixture;

struct Opened {
  pdfkit_document* doc = nullptr;
  std::string file;
  Opened(const Opened&) = delete;
  Opened& operator=(const Opened&) = delete;
  Opened(Opened&&) = delete;
  Opened& operator=(Opened&&) = delete;
  explicit Opened(std::string bytes) : file(std::move(bytes)) {
    pdfkit_buffer out{nullptr, 0, nullptr};
    const pdfkit_status status = pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()),
                                             file.size(), nullptr, &doc, &out);
    pdfkit_buffer_free(&out);
    REQUIRE(status == PDFKIT_OK);
  }
  ~Opened() { pdfkit_close(doc); }
};

std::string take(pdfkit_buffer& buffer) {
  std::string text(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  return text;
}

void check_error(const std::string& text, const std::string& code, const std::string& part) {
  INFO(text);
  const JsonValue error = JsonReader::parse(text);
  CHECK(error.find("code")->as_string() == code);
  for (const char* field : {"failed", "expected", "fix"}) {
    REQUIRE(error.find(field) != nullptr);
    CHECK_FALSE(error.find(field)->as_string().empty());
  }
  CHECK(error.find("failed")->as_string().find(part) != std::string::npos);
}

MetadataFixture sample() {
  MetadataFixture made;
  made.info = "/Title (Same) /Producer (Info maker) /CreationDate (D:20200102030405Z)";
  made.packet = MetadataFixture::xmp(
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>Same</rdf:li></rdf:Alt></dc:title>\n"
      "<pdf:Producer>XMP maker</pdf:Producer>\n"
      "<xmp:CreateDate>2020-01-02T03:04:05Z</xmp:CreateDate>\n");
  return made;
}

}  // namespace

TEST_CASE("pdfkit_metadata answers one row per field side by side") {
  const Opened opened(sample().build());
  pdfkit_buffer out{nullptr, 0, nullptr};
  REQUIRE(pdfkit_metadata(opened.doc, nullptr, &out) == PDFKIT_OK);
  const std::string text = take(out);
  INFO(text);
  const JsonValue answer = JsonReader::parse(text);
  CHECK(answer.find("info")->as_bool());
  CHECK(answer.find("xmp")->as_string() == "read");
  CHECK(answer.find("xmp_fault")->kind() == JsonValue::Kind::null);
  CHECK_FALSE(answer.find("agree")->as_bool());
  const auto& fields = answer.find("fields")->as_array();
  REQUIRE(fields.size() == 8);
  CHECK(fields[0].find("field")->as_string() == "Title");
  CHECK(fields[0].find("info")->as_string() == "Same");
  CHECK(fields[0].find("info_encoding")->as_string() == "pdf_doc");
  CHECK(fields[0].find("xmp_property")->as_string() == "dc:title");
  CHECK(fields[0].find("agree")->as_bool());
  CHECK(fields[1].find("agree")->kind() == JsonValue::Kind::null);
  CHECK(fields[5].find("field")->as_string() == "Producer");
  CHECK_FALSE(fields[5].find("agree")->as_bool());
  CHECK(fields[6].find("info_iso")->as_string() == "2020-01-02T03:04:05Z");
  CHECK(fields[6].find("agree")->as_bool());
  CHECK(fields[0].find("info_iso") == nullptr);
  CHECK(answer.find("warnings")->as_array().empty());
  const auto& namespaces = answer.find("xmp_namespaces")->as_array();
  REQUIRE(namespaces.size() == 3);
  CHECK(namespaces[0].as_string() == "http://ns.adobe.com/pdf/1.3/");
  CHECK_FALSE(answer.find("xmp_history")->as_bool());
  CHECK(fields[0].find("xmp_values")->as_array()[0].find("items")->as_array()[0].as_string() ==
        "Same");

  REQUIRE(pdfkit_metadata(opened.doc, R"({"pages": [1]})", &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(take(out), "invalid_argument", "pages");
}

TEST_CASE("pdfkit_metadata reports an unreadable packet") {
  MetadataFixture made;
  made.packet = MetadataFixture::xmp("<dc:title>x</dc:creator>\n");
  const Opened opened(made.build());
  pdfkit_buffer out{nullptr, 0, nullptr};
  REQUIRE(pdfkit_metadata(opened.doc, "{}", &out) == PDFKIT_OK);
  const JsonValue answer = JsonReader::parse(take(out));
  CHECK(answer.find("xmp")->as_string() == "unreadable");
  CHECK_FALSE(answer.find("xmp_fault")->as_string().empty());
  REQUIRE(answer.find("warnings")->as_array().size() == 1);
  CHECK(answer.find("warnings")->as_array()[0].find("code")->as_string() == "xmp_unreadable");
}

TEST_CASE("pdfkit_set_metadata answers the fields as they will be written") {
  const Opened opened(sample().build());
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_edit* edit = nullptr;
  REQUIRE(pdfkit_edit_new(opened.doc, &edit, &out) == PDFKIT_OK);
  pdfkit_buffer_free(&out);
  REQUIRE(pdfkit_set_metadata(edit, R"({"Keywords": "a, b", "Producer": null})", &out) ==
          PDFKIT_OK);
  const std::string text = take(out);
  INFO(text);
  const JsonValue answer = JsonReader::parse(text);
  const auto& fields = answer.find("fields")->as_array();
  REQUIRE(fields.size() == 2);
  CHECK(fields[0].find("field")->as_string() == "Keywords");
  CHECK(fields[0].find("info")->as_string() == "a, b");
  CHECK(fields[0].find("xmp_properties")->as_array()[0].as_string() == "pdf:Keywords");
  CHECK(fields[1].find("removed")->as_bool());
  CHECK(fields[1].find("xmp_properties")->as_array()[0].as_string() == "pdf:Producer");

  REQUIRE(pdfkit_set_metadata(edit, R"({"Company": "x"})", &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(take(out), "invalid_argument", "Company");
  REQUIRE(pdfkit_set_metadata(edit, R"({"Title": 7})", &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(take(out), "invalid_argument", "Title");
  REQUIRE(pdfkit_set_metadata(edit, nullptr, &out) == PDFKIT_E_INVALID_ARGUMENT);
  check_error(take(out), "invalid_argument", "fields_json");
  pdfkit_edit_free(edit);
}
