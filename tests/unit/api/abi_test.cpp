// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the C ABI surface: every catalogue symbol refuses null arguments
// with PDFKIT_E_INVALID_ARGUMENT, answers valid arguments with a status and an
// actionable JSON error, and never lets an exception escape; the lifecycle
// symbols work; and pdfkit_version reports the catalogue.

#include "pdfkit/pdfkit.h"

#include "api/catalogue.hpp"
#include "api/guarded.hpp"
#include "api/handles.hpp"
#include "platform/error.hpp"
#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "support/pdf_builder.hpp"

#include <doctest.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;

// A buffer freed when the test ends, whatever it holds.
class OwnedBuffer {
 public:
  OwnedBuffer() = default;
  OwnedBuffer(const OwnedBuffer&) = delete;
  OwnedBuffer& operator=(const OwnedBuffer&) = delete;
  OwnedBuffer(OwnedBuffer&&) = delete;
  OwnedBuffer& operator=(OwnedBuffer&&) = delete;
  ~OwnedBuffer() { pdfkit_buffer_free(&buffer_); }
  pdfkit_buffer* get() { return &buffer_; }
  [[nodiscard]] std::string_view text() const {
    return {reinterpret_cast<const char*>(buffer_.data), buffer_.size};
  }

 private:
  pdfkit_buffer buffer_{nullptr, 0, nullptr};
};

void check_error_object(std::string_view text, std::string_view code) {
  const JsonValue error = JsonReader::parse(text);
  REQUIRE(error.kind() == JsonValue::Kind::object);
  CHECK(error.find("code")->as_string() == code);
  CHECK_FALSE(error.find("failed")->as_string().empty());
  CHECK_FALSE(error.find("expected")->as_string().empty());
  CHECK_FALSE(error.find("fix")->as_string().empty());
}

// A one-page document opened through the C ABI, an edit over it, and four
// bytes that are not a PDF.
struct Fixture {
  Fixture() {
    pdfkit::test::PdfBuilder builder;
    builder.begin_document();
    builder.add_page("BT ET");
    const std::string file = builder.build();
    pdfkit_buffer out{nullptr, 0, nullptr};
    pdfkit_document* opened = nullptr;
    REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                        &opened, &out) == PDFKIT_OK);
    pdfkit_buffer_free(&out);
    document.reset(opened);
    edit = pdfkit::api::new_edit(*document);
  }
  std::unique_ptr<pdfkit_document> document;
  std::unique_ptr<pdfkit_edit> edit;
  std::vector<std::uint8_t> bytes{'%', 'P', 'D', 'F'};
};

using Call = std::function<pdfkit_status(pdfkit_buffer*)>;

struct SymbolCalls {
  std::string_view symbol;
  Call with_nulls;
  Call with_valid_arguments;
};

std::vector<SymbolCalls> catalogue_calls(Fixture& f) {
  pdfkit_document* doc = f.document.get();
  pdfkit_edit* edit = f.edit.get();
  const std::uint8_t* bytes = f.bytes.data();
  const std::size_t size = f.bytes.size();
  return {
      {"pdfkit_open",
       [](pdfkit_buffer* out) { return pdfkit_open(nullptr, 4, nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) {
         pdfkit_document* opened = nullptr;
         const pdfkit_status status = pdfkit_open(bytes, size, "{}", &opened, out);
         pdfkit_close(opened);
         return status;
       }},
      {"pdfkit_inspect", [](pdfkit_buffer* out) { return pdfkit_inspect(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_inspect(doc, nullptr, out); }},
      {"pdfkit_text", [](pdfkit_buffer* out) { return pdfkit_text(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_text(doc, "{}", out); }},
      {"pdfkit_images", [](pdfkit_buffer* out) { return pdfkit_images(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_images(doc, "{}", out); }},
      {"pdfkit_image_data",
       [](pdfkit_buffer* out) { return pdfkit_image_data(nullptr, nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) {
         return pdfkit_image_data(doc, "img:0123456789abcdef", "{}", out);
       }},
      {"pdfkit_metadata", [](pdfkit_buffer* out) { return pdfkit_metadata(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_metadata(doc, nullptr, out); }},
      {"pdfkit_redact_text",
       [](pdfkit_buffer* out) { return pdfkit_redact_text(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_redact_text(edit, "{\"matches\":[]}", out); }},
      {"pdfkit_redact_images",
       [](pdfkit_buffer* out) { return pdfkit_redact_images(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_redact_images(edit, "{\"treatments\":[]}", out); }},
      {"pdfkit_set_metadata",
       [](pdfkit_buffer* out) { return pdfkit_set_metadata(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_set_metadata(edit, "{\"fields\":{}}", out); }},
      {"pdfkit_channels", [](pdfkit_buffer* out) { return pdfkit_channels(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_channels(doc, nullptr, out); }},
      {"pdfkit_select_pages",
       [](pdfkit_buffer* out) { return pdfkit_select_pages(nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_select_pages(edit, "{\"pages\":[1]}", out); }},
      {"pdfkit_write",
       [](pdfkit_buffer* out) { return pdfkit_write(nullptr, nullptr, nullptr, out); },
       [=](pdfkit_buffer* out) {
         OwnedBuffer pdf;
         return pdfkit_write(edit, nullptr, pdf.get(), out);
       }},
      {"pdfkit_check", [](pdfkit_buffer* out) { return pdfkit_check(nullptr, 4, nullptr, out); },
       [=](pdfkit_buffer* out) { return pdfkit_check(bytes, size, "{\"strings\":[]}", out); }},
  };
}

TEST_CASE("the ABI version is 1") {
  CHECK(pdfkit_abi_version() == 1U);
}

TEST_CASE("every catalogue symbol is exercised by this test") {
  Fixture fixture;
  const auto calls = catalogue_calls(fixture);
  REQUIRE(calls.size() == pdfkit::api::Catalogue::rows().size());
  for (std::size_t i = 0; i < calls.size(); ++i) {
    CHECK(calls[i].symbol == pdfkit::api::Catalogue::rows()[i].symbol);
  }
}

TEST_CASE("every catalogue symbol refuses null arguments") {
  Fixture fixture;
  for (const SymbolCalls& call : catalogue_calls(fixture)) {
    CAPTURE(call.symbol);
    OwnedBuffer out;
    CHECK(call.with_nulls(out.get()) == PDFKIT_E_INVALID_ARGUMENT);
    check_error_object(out.text(), "invalid_argument");
    CHECK(out.text().find(call.symbol) != std::string_view::npos);
    CHECK(call.with_nulls(nullptr) == PDFKIT_E_INVALID_ARGUMENT);
  }
}

TEST_CASE("every catalogue symbol not yet delivered says so and names its build file") {
  Fixture fixture;
  for (const SymbolCalls& call : catalogue_calls(fixture)) {
    CAPTURE(call.symbol);
    const auto& row = pdfkit::api::Catalogue::find(call.symbol);
    if (row.status != pdfkit::api::Catalogue::Status::not_implemented) {
      continue;
    }
    OwnedBuffer out;
    CHECK(call.with_valid_arguments(out.get()) == PDFKIT_E_NOT_IMPLEMENTED);
    check_error_object(out.text(), "not_implemented");
    CHECK(out.text().find(call.symbol) != std::string_view::npos);
    CHECK(out.text().find(row.build_file) != std::string_view::npos);
  }
}

TEST_CASE("malformed options are refused naming the byte offset") {
  Fixture fixture;
  OwnedBuffer out;
  CHECK(pdfkit_text(fixture.document.get(), "{\"pages\": [1,]}", out.get()) ==
        PDFKIT_E_INVALID_ARGUMENT);
  check_error_object(out.text(), "invalid_argument");
  CHECK(out.text().find("byte 13") != std::string_view::npos);

  OwnedBuffer not_object;
  CHECK(pdfkit_text(fixture.document.get(), "[]", not_object.get()) == PDFKIT_E_INVALID_ARGUMENT);
  check_error_object(not_object.text(), "invalid_argument");
}

TEST_CASE("pdfkit_version reports the version, the ABI and the catalogue") {
  OwnedBuffer out;
  REQUIRE(pdfkit_version(out.get()) == PDFKIT_OK);
  const JsonValue version = JsonReader::parse(out.text());
  CHECK_FALSE(version.find("version")->as_string().empty());
  CHECK(version.find("abi")->as_int64() == std::int64_t{1});
  CHECK_FALSE(version.find("build")->as_string().empty());
  const auto& catalogue = version.find("catalogue")->as_array();
  REQUIRE(catalogue.size() == pdfkit::api::Catalogue::rows().size());
  for (const JsonValue& row : catalogue) {
    CAPTURE(row.find("symbol")->as_string());
    CHECK_FALSE(row.find("number")->as_string().empty());
    CHECK_FALSE(row.find("function")->as_string().empty());
    CHECK_FALSE(row.find("verb")->as_string().empty());
    CHECK_FALSE(row.find("build_file")->as_string().empty());
    const JsonValue* error = row.find("error");
    if (row.find("implemented")->as_bool()) {
      CHECK(error == nullptr);
    } else {
      REQUIRE(error != nullptr);
      CHECK(error->find("code")->as_string() == "not_implemented");
      CHECK(error->find("failed")->as_string().find(row.find("symbol")->as_string()) !=
            std::string::npos);
      CHECK_FALSE(error->find("expected")->as_string().empty());
      CHECK_FALSE(error->find("fix")->as_string().empty());
    }
  }
  CHECK(pdfkit_version(nullptr) == PDFKIT_E_INVALID_ARGUMENT);
}

TEST_CASE("the buffer is NUL-terminated and freeing it twice or empty is a no-op") {
  OwnedBuffer out;
  REQUIRE(pdfkit_version(out.get()) == PDFKIT_OK);
  CHECK(out.get()->data[out.get()->size] == 0);
  pdfkit_buffer_free(out.get());
  CHECK(out.get()->data == nullptr);
  CHECK(out.get()->size == 0U);
  CHECK(out.get()->reserved == nullptr);
  pdfkit_buffer_free(out.get());
  pdfkit_buffer_free(nullptr);
}

TEST_CASE("an edit handle is created over a document and freed") {
  Fixture fixture;
  OwnedBuffer out;
  pdfkit_edit* edit = nullptr;
  REQUIRE(pdfkit_edit_new(fixture.document.get(), &edit, out.get()) == PDFKIT_OK);
  CHECK(edit != nullptr);
  pdfkit_edit_free(edit);
  pdfkit_edit_free(nullptr);
  pdfkit_close(nullptr);

  OwnedBuffer refused;
  CHECK(pdfkit_edit_new(nullptr, &edit, refused.get()) == PDFKIT_E_INVALID_ARGUMENT);
  check_error_object(refused.text(), "invalid_argument");
  CHECK(pdfkit_edit_new(fixture.document.get(), nullptr, refused.get()) ==
        PDFKIT_E_INVALID_ARGUMENT);
}

TEST_CASE("guarded maps every failure to a status and never lets it escape") {
  using pdfkit::platform::Error;
  using pdfkit::platform::ErrorCode;
  struct Case {
    std::function<pdfkit_status()> body;
    pdfkit_status status;
    std::string_view code;
  };
  const std::vector<Case> cases{
      {[]() -> pdfkit_status {
         throw Error(ErrorCode::not_pdf, "the bytes are not a PDF", "a %PDF- header",
                     "re-export the file");
       },
       PDFKIT_E_REFUSED, "not_pdf"},
      {[]() -> pdfkit_status {
         throw Error(ErrorCode::self_check, "a redacted string survives on page 2", "no survivor",
                     "add the surviving occurrence to the matches");
       },
       PDFKIT_E_SELF_CHECK, "self_check"},
      {[]() -> pdfkit_status { throw std::runtime_error("boom"); }, PDFKIT_E_INTERNAL, "internal"},
      {[]() -> pdfkit_status { throw std::bad_alloc(); }, PDFKIT_E_INTERNAL, "internal"},
      // An exception of a type outside std::exception, raised through an
      // exception_ptr so the test itself throws no such type.
      {[]() -> pdfkit_status { std::rethrow_exception(std::make_exception_ptr(42)); },
       PDFKIT_E_INTERNAL, "internal"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.code);
    OwnedBuffer out;
    CHECK(pdfkit::api::guarded(out.get(), c.body) == c.status);
    check_error_object(out.text(), c.code);
    CHECK(pdfkit::api::guarded(nullptr, c.body) == c.status);
  }
}

}  // namespace
