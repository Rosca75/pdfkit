// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::SecurityHandler through syntax::Document: one clear
// fixture encrypted by the test tree's own encryptor under revisions 2, 3, 4
// (RC4 40, RC4 128, RC4 and AESV2 through crypt filters), 5 and 6 (AESV3)
// reads as the clear one; /EncryptMetadata false leaves the metadata stream
// clear; /Identity crypt filters decrypt nothing; a non-empty user password
// is password_required and a handler other than /Standard is
// unsupported_security_handler, each with its three message parts.

#include "syntax/security_handler.hpp"

#include "platform/error.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "support/pdf_encryptor.hpp"
#include "support/require_value.hpp"
#include "syntax/document.hpp"

#include <doctest.h>

#include <string>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::syntax::Document;
using pdfkit::test::open_document;
using pdfkit::test::page_text;
using pdfkit::test::PdfBuilder;
using pdfkit::test::PdfEncryptor;
using pdfkit::test::require_value;

constexpr const char* content = "BT /F1 12 Tf 72 700 Td (Confidential text) Tj ET";

struct Fixture {
  PdfBuilder builder;
  std::uint32_t info = 0;
  std::uint32_t metadata = 0;
};

Fixture fixture() {
  Fixture made;
  made.builder.begin_document();
  made.builder.add_page(content, "", true);
  made.builder.add_page(content, "", false);
  made.info = made.builder.add("<< /Title " + PdfBuilder::str("A title (with parentheses)") +
                               " /Producer " + PdfBuilder::str("pdfkit tests") + " >>");
  made.builder.set_info(made.info);
  made.metadata = made.builder.add_stream("/Type /Metadata /Subtype /XML", "<x:xmpmeta/>");
  return made;
}

std::string title_of(Document& document) {
  const pdfkit::syntax::Object info = document.info();
  REQUIRE(info.dictionary() != nullptr);
  const pdfkit::syntax::Object::String* title = info.dictionary()->get("Title").string();
  REQUIRE(title != nullptr);
  return title->bytes;
}

std::string decoded(Document& document, std::uint32_t number) {
  const pdfkit::syntax::Object value = document.object({number, 0});
  REQUIRE(value.stream() != nullptr);
  const auto result = document.decode(*value.stream());
  return {reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size()};
}

void check_reads_clear(const PdfEncryptor::Spec& spec, const std::string& method) {
  Fixture made = fixture();
  made.builder.encrypt(spec);
  const std::string file = made.builder.build();
  CHECK(file.find("Confidential") == std::string::npos);
  auto document = open_document(file);
  CHECK(require_value(document->security()).description().revision == spec.revision);
  CHECK(require_value(document->security()).description().method == method);
  CHECK(document->repairs().empty());
  CHECK(page_text(*document, 1) == content);
  CHECK(page_text(*document, 2) == content);
  CHECK(title_of(*document) == "A title (with parentheses)");
  CHECK(decoded(*document, made.metadata) == "<x:xmpmeta/>");
}

TEST_CASE("revisions 2 and 3: RC4 with 40- and 128-bit keys") {
  PdfEncryptor::Spec r2;
  r2.revision = 2;
  r2.method = PdfEncryptor::Method::rc4;
  check_reads_clear(r2, "RC4");
  PdfEncryptor::Spec r3;
  r3.revision = 3;
  r3.key_bits = 128;
  r3.method = PdfEncryptor::Method::rc4;
  check_reads_clear(r3, "RC4");
  r3.key_bits = 56;
  check_reads_clear(r3, "RC4");
}

TEST_CASE("revision 4: RC4 and AESV2 through the standard crypt filter") {
  PdfEncryptor::Spec rc4;
  rc4.revision = 4;
  rc4.method = PdfEncryptor::Method::rc4;
  check_reads_clear(rc4, "RC4");
  PdfEncryptor::Spec aes;
  aes.revision = 4;
  aes.method = PdfEncryptor::Method::aes;
  check_reads_clear(aes, "AESV2");
}

TEST_CASE("revisions 5 and 6: AESV3") {
  PdfEncryptor::Spec r5;
  r5.revision = 5;
  check_reads_clear(r5, "AESV3");
  PdfEncryptor::Spec r6;
  r6.revision = 6;
  check_reads_clear(r6, "AESV3");
}

TEST_CASE("/EncryptMetadata false leaves the metadata stream clear and changes the key") {
  for (const int revision : {4, 6}) {
    PdfEncryptor::Spec spec;
    spec.revision = revision;
    spec.encrypt_metadata = false;
    Fixture made = fixture();
    made.builder.encrypt(spec);
    const std::string file = made.builder.build();
    CHECK(file.find("<x:xmpmeta/>") != std::string::npos);
    auto document = open_document(file);
    CHECK_FALSE(require_value(document->security()).description().encrypt_metadata);
    CHECK(page_text(*document, 1) == content);
    CHECK(decoded(*document, made.metadata) == "<x:xmpmeta/>");
  }
}

TEST_CASE("/Identity crypt filters decrypt nothing") {
  PdfEncryptor::Spec spec;
  spec.revision = 4;
  spec.stream_filter = "Identity";
  spec.string_filter = "Identity";
  Fixture made = fixture();
  made.builder.encrypt(spec);
  const std::string file = made.builder.build();
  CHECK(file.find("Confidential") != std::string::npos);
  auto document = open_document(file);
  CHECK(require_value(document->security()).description().method == "Identity");
  CHECK(page_text(*document, 2) == content);
  CHECK(title_of(*document) == "A title (with parentheses)");
}

TEST_CASE("a corrupt /Perms is a repair note, never a refusal") {
  PdfEncryptor::Spec spec;
  spec.revision = 6;
  spec.corrupt_perms = true;
  Fixture made = fixture();
  made.builder.encrypt(spec);
  auto document = open_document(made.builder.build());
  CHECK(require_value(document->security()).perms_mismatch());
  CHECK(page_text(*document, 1) == content);
  CHECK(document->repairs().size() == 1U);
}

TEST_CASE("a non-empty user password is password_required under every revision") {
  for (const int revision : {2, 3, 4, 5, 6}) {
    PdfEncryptor::Spec spec;
    spec.revision = revision;
    spec.method = revision < 4 ? PdfEncryptor::Method::rc4 : PdfEncryptor::Method::aes;
    spec.user_password = "secret";
    Fixture made = fixture();
    made.builder.encrypt(spec);
    try {
      (void)open_document(made.builder.build());
      FAIL("a file with a user password was opened");
    } catch (const Error& error) {
      CHECK(error.code() == ErrorCode::password_required);
      CHECK(error.failed().find("revision " + std::to_string(revision)) != std::string::npos);
      CHECK(error.expected().find("empty user password") != std::string::npos);
      CHECK(error.fix().find("password") != std::string::npos);
    }
  }
}

TEST_CASE("a handler other than /Standard, or an unknown revision, is unsupported") {
  for (const std::string& dictionary :
       {std::string("<< /Filter /Adobe.PubSec /V 4 /R 4 /SubFilter /adbe.pkcs7.s5 >>"),
        std::string("<< /Filter /Standard /V 4 /R 7 /O <00> /U <00> /P -4 >>"),
        std::string("<< /Filter /Standard /V 3 /R 3 /O <00> /U <00> /P -4 >>")}) {
    PdfBuilder builder;
    builder.begin_document();
    builder.add_page("BT ET");
    const std::uint32_t encrypt = builder.add(dictionary);
    builder.add_trailer_entries("/Encrypt " + std::to_string(encrypt) + " 0 R");
    try {
      (void)open_document(builder.build());
      FAIL("an unsupported handler was opened");
    } catch (const Error& error) {
      CHECK(error.code() == ErrorCode::unsupported_security_handler);
      CHECK_FALSE(error.failed().empty());
      CHECK(error.fix().find("remove the protection") != std::string::npos);
    }
  }
}

}  // namespace
