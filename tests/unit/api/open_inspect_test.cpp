// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of pdfkit_open and pdfkit_inspect through the C ABI over builder
// fixtures: the open report and the inspect JSON in their documented shapes
// and key order, the options and their refusals, and every refusal code of
// docs/DESIGN.md chapter 19 reaching the caller as a JSON error object with
// its three message parts.

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "support/pdf_builder.hpp"
#include "support/pdf_encryptor.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;
using pdfkit::test::PdfBuilder;

struct Opened {
  pdfkit_status status = PDFKIT_E_INTERNAL;
  std::string text;
  pdfkit_document* document = nullptr;

  Opened() = default;
  Opened(const Opened&) = delete;
  Opened& operator=(const Opened&) = delete;
  Opened(Opened&&) = delete;
  Opened& operator=(Opened&&) = delete;
  ~Opened() { pdfkit_close(document); }
};

void open(Opened& opened, const std::string& file, const char* options = nullptr) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  opened.status = pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(),
                              options, &opened.document, &out);
  opened.text.assign(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
}

std::string inspect(pdfkit_document* document, const char* options = nullptr) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_inspect(document, options, &out);
  std::string text(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  CHECK(status == (options == nullptr || std::string_view(options) == "{}"
                       ? PDFKIT_OK
                       : PDFKIT_E_INVALID_ARGUMENT));
  return text;
}

std::string keys_of(const JsonValue& object) {
  std::string out;
  for (const auto& member : object.as_object()) {
    out += member.first + " ";
  }
  return out;
}

void check_refusal(const Opened& opened, std::string_view code) {
  REQUIRE(opened.status == PDFKIT_E_REFUSED);
  CHECK(opened.document == nullptr);
  const JsonValue error = JsonReader::parse(opened.text);
  CHECK(error.find("code")->as_string() == code);
  CHECK_FALSE(error.find("failed")->as_string().empty());
  CHECK_FALSE(error.find("expected")->as_string().empty());
  CHECK_FALSE(error.find("fix")->as_string().empty());
}

PdfBuilder two_pages() {
  PdfBuilder builder;
  builder.begin_document();
  builder.add_page("BT ET", "/Rotate 90 /CropBox [10 20 300 400]");
  builder.add_page("BT ET");
  return builder;
}

TEST_CASE("the open report and the inspect output in their documented shapes") {
  Opened opened;
  open(opened, two_pages().build());
  REQUIRE(opened.status == PDFKIT_OK);
  REQUIRE(opened.document != nullptr);
  const JsonValue report = JsonReader::parse(opened.text);
  CHECK(keys_of(report) == "version page_count encryption repairs warnings ");
  CHECK(report.find("version")->as_string() == "1.7");
  CHECK(report.find("page_count")->as_int64() == std::int64_t{2});
  CHECK(report.find("encryption")->kind() == JsonValue::Kind::null);
  CHECK(report.find("repairs")->as_array().empty());
  CHECK(report.find("warnings")->as_array().empty());

  const JsonValue output = JsonReader::parse(inspect(opened.document));
  CHECK(keys_of(output) == "version page_count pages encryption repairs warnings ");
  const auto& pages = output.find("pages")->as_array();
  REQUIRE(pages.size() == 2U);
  CHECK(keys_of(pages[0]) == "page media_box crop_box rotate ");
  CHECK(pages[0].find("page")->as_int64() == std::int64_t{1});
  CHECK(pages[0].find("rotate")->as_int64() == std::int64_t{90});
  const auto& crop = pages[0].find("crop_box")->as_array();
  REQUIRE(crop.size() == 4U);
  CHECK(crop[0].as_number() == doctest::Approx(10));
  CHECK(crop[3].as_number() == doctest::Approx(400));
  CHECK(pages[1].find("media_box")->as_array()[2].as_number() == doctest::Approx(612));
  // Two calls give the same bytes.
  CHECK(inspect(opened.document) == inspect(opened.document, "{}"));
}

TEST_CASE("a repaired and an encrypted file say so in both outputs") {
  PdfBuilder builder = two_pages();
  pdfkit::test::PdfEncryptor::Spec spec;
  spec.revision = 4;
  builder.encrypt(spec);
  pdfkit::test::Layout layout;
  layout.startxref = 33;
  Opened opened;
  open(opened, builder.build(layout));
  REQUIRE(opened.status == PDFKIT_OK);
  const JsonValue report = JsonReader::parse(opened.text);
  const JsonValue* encryption = report.find("encryption");
  REQUIRE(encryption->kind() == JsonValue::Kind::object);
  CHECK(keys_of(*encryption) == "filter version revision method key_bits encrypt_metadata ");
  CHECK(encryption->find("revision")->as_int64() == std::int64_t{4});
  CHECK(encryption->find("method")->as_string() == "AESV2");
  CHECK_FALSE(report.find("repairs")->as_array().empty());
  const auto& warnings = report.find("warnings")->as_array();
  REQUIRE_FALSE(warnings.empty());
  CHECK(keys_of(warnings[0]) == "code message page reasons ");
  CHECK(warnings[0].find("code")->as_string() == "repaired");
  const JsonValue output = JsonReader::parse(inspect(opened.document));
  CHECK(output.find("repairs")->as_array().size() == report.find("repairs")->as_array().size());
}

TEST_CASE("the options set the limits and refuse what they do not know") {
  PdfBuilder builder = two_pages();
  Opened ceiling;
  open(ceiling, builder.build(), R"({"page_ceiling": 1})");
  check_refusal(ceiling, "page_ceiling");
  // Open decodes the compressed cross-reference stream, past 10 bytes.
  Opened tiny_stream;
  pdfkit::test::Layout compressed;
  compressed.xref = pdfkit::test::XrefForm::stream;
  compressed.xref_stream_predictor = true;
  open(tiny_stream, builder.build(compressed), R"({"stream_bytes": 10})");
  check_refusal(tiny_stream, "limit_exceeded");
  Opened raised;
  open(raised, builder.build(), R"({"page_ceiling": 2, "objects": 100, "document_bytes": 99999})");
  CHECK(raised.status == PDFKIT_OK);

  for (const char* bad : {R"({"page_limit": 5})", R"({"page_ceiling": 0})",
                          R"({"page_ceiling": "5"})", R"({"objects": 1.5})"}) {
    CAPTURE(bad);
    Opened refused;
    open(refused, builder.build(), bad);
    CHECK(refused.status == PDFKIT_E_INVALID_ARGUMENT);
    CHECK(JsonReader::parse(refused.text).find("code")->as_string() == "invalid_argument");
  }
}

TEST_CASE("every refusal of chapter 19 reaches the caller with its three parts") {
  Opened empty;
  pdfkit_buffer out{nullptr, 0, nullptr};
  empty.status = pdfkit_open(nullptr, 0, nullptr, &empty.document, &out);
  empty.text.assign(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  check_refusal(empty, "not_pdf");

  Opened no_page;
  PdfBuilder catalog_only;
  catalog_only.add("<< /Type /Catalog >>");
  open(no_page, catalog_only.build());
  check_refusal(no_page, "no_page");

  Opened password;
  PdfBuilder protected_file = two_pages();
  pdfkit::test::PdfEncryptor::Spec spec;
  spec.revision = 6;
  spec.user_password = "secret";
  protected_file.encrypt(spec);
  open(password, protected_file.build());
  check_refusal(password, "password_required");

  Opened handler;
  PdfBuilder public_key = two_pages();
  const std::uint32_t encrypt = public_key.add("<< /Filter /Adobe.PubSec /V 4 /R 4 >>");
  public_key.add_trailer_entries("/Encrypt " + std::to_string(encrypt) + " 0 R");
  open(handler, public_key.build());
  check_refusal(handler, "unsupported_security_handler");
}

TEST_CASE("inspect refuses options it does not take") {
  Opened opened;
  open(opened, two_pages().build());
  REQUIRE(opened.status == PDFKIT_OK);
  const JsonValue error = JsonReader::parse(inspect(opened.document, R"({"pages": [1]})"));
  CHECK(error.find("code")->as_string() == "invalid_argument");
}

}  // namespace
