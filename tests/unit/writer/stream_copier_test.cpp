// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the stream rules of the writer: an untouched stream is copied byte
// for byte with its filters, a filter chain is kept, a stream of an encrypted
// source is written decrypted with its filters and reads back in clear, a
// /Crypt filter goes with the encryption, and a rewritten stream is
// Flate-compressed with a right /Length.

#include "writer/stream_copier.hpp"

#include "platform/byte_span.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "support/pdf_encryptor.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"
#include "writer/file_writer.hpp"
#include "writer/object_graph.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>

namespace {

using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::test::PdfBuilder;
using pdfkit::test::PdfEncryptor;

std::string_view content() {
  return "BT /F1 12 Tf 72 720 Td (Stream copier content) Tj ET";
}

std::string write_identity(const std::string& file) {
  const auto document = pdfkit::test::open_document(file);
  pdfkit::writer::ObjectGraph graph(*document);
  return pdfkit::writer::FileWriter::write(graph).bytes;
}

// ASCII85 (ISO 32000-1 section 7.4.3) of data with its end marker.
std::string ascii85(const std::string& data) {
  std::string out;
  for (std::size_t i = 0; i < data.size(); i += 4) {
    std::uint32_t value = 0;
    const std::size_t n = std::min<std::size_t>(4, data.size() - i);
    for (std::size_t k = 0; k < 4; ++k) {
      value = (value << 8U) | (k < n ? static_cast<unsigned char>(data[i + k]) : 0U);
    }
    std::string group(5, '!');
    for (int k = 4; k >= 0; --k) {
      group[static_cast<std::size_t>(k)] = static_cast<char>('!' + value % 85);
      value /= 85;
    }
    out += group.substr(0, n + 1);
  }
  return out + "~>";
}

std::string first_page_content(const std::string& output) {
  const auto document = pdfkit::test::reopen(output);
  return pdfkit::test::page_text(*document, 1);
}

}  // namespace

TEST_CASE("an untouched Flate stream is copied byte for byte with its filter") {
  PdfBuilder builder;
  builder.begin_document();
  (void)builder.add_page(std::string(content()), "", true);
  const std::string output = write_identity(builder.build());
  CHECK(output.find(PdfBuilder::deflate(std::string(content()))) != std::string::npos);
  CHECK(output.find("/Filter /FlateDecode") != std::string::npos);
  CHECK(first_page_content(output) == std::string(content()));
}

TEST_CASE("an ASCII85 and Flate chain is kept") {
  PdfBuilder builder;
  builder.begin_document();
  const std::string encoded = ascii85(PdfBuilder::deflate(std::string(content())));
  const std::uint32_t stream = builder.add_stream("/Filter [/ASCII85Decode /FlateDecode]", encoded);
  const std::uint32_t page = builder.reserve();
  builder.set(page, "<< /Type /Page /Parent " + std::to_string(builder.page_tree()) +
                        " 0 R /MediaBox [0 0 612 792] /Contents " + std::to_string(stream) +
                        " 0 R >>");
  builder.set(builder.page_tree(),
              "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R] /Count 1 >>");
  const std::string output = write_identity(builder.build());
  CHECK(output.find(encoded) != std::string::npos);
  CHECK(output.find("/Filter [/ASCII85Decode /FlateDecode]") != std::string::npos);
  CHECK(first_page_content(output) == std::string(content()));
}

TEST_CASE("streams of an encrypted source are written decrypted with their filters") {
  struct Case {
    const char* name;
    PdfEncryptor::Spec spec;
  };
  PdfEncryptor::Spec rc4;
  rc4.revision = 3;
  rc4.key_bits = 128;
  rc4.method = PdfEncryptor::Method::rc4;
  PdfEncryptor::Spec aes256;
  aes256.revision = 6;
  aes256.key_bits = 256;
  aes256.method = PdfEncryptor::Method::aes;
  for (const Case& test : {Case{"RC4", rc4}, Case{"AESV3", aes256}}) {
    CAPTURE(test.name);
    PdfBuilder builder;
    builder.begin_document();
    (void)builder.add_page(std::string(content()), "", true);
    const std::uint32_t info =
        builder.add("<< /Title " + PdfBuilder::str("Encrypted title") + " >>");
    builder.set_info(info);
    builder.encrypt(test.spec);
    const std::string output = write_identity(builder.build());
    const auto document = pdfkit::test::reopen(output);
    CHECK_FALSE(document->security().has_value());
    CHECK_FALSE(document->trailer().contains("Encrypt"));
    CHECK(pdfkit::test::page_text(*document, 1) == std::string(content()));
    CHECK(output.find(PdfBuilder::deflate(std::string(content()))) != std::string::npos);
    const Object title = document->resolve_entry(*document->info().dictionary(), "Title");
    REQUIRE(title.string() != nullptr);
    CHECK(title.string()->bytes == "Encrypted title");
  }
}

TEST_CASE("a /Crypt filter goes with the encryption") {
  PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t stream =
      builder.add_stream("/Filter [/Crypt /FlateDecode] /DecodeParms [<< /Name /Identity >> null]",
                         PdfBuilder::deflate(std::string(content())));
  const std::uint32_t page = builder.reserve();
  builder.set(page, "<< /Type /Page /Parent " + std::to_string(builder.page_tree()) +
                        " 0 R /MediaBox [0 0 612 792] /Contents " + std::to_string(stream) +
                        " 0 R >>");
  builder.set(builder.page_tree(),
              "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R] /Count 1 >>");
  PdfEncryptor::Spec spec;
  spec.revision = 4;
  spec.stream_filter = "Identity";
  builder.encrypt(spec);
  const std::string output = write_identity(builder.build());
  CHECK(output.find("/Crypt") == std::string::npos);
  CHECK(output.find("/Filter [/FlateDecode] /DecodeParms [null]") != std::string::npos);
  CHECK(first_page_content(output) == std::string(content()));

  // A created stream is never decrypted, so its filters stay as written.
  Dictionary dictionary;
  dictionary.set(
      "Filter", Object::make_array({Object::make_name("Crypt"), Object::make_name("FlateDecode")}));
  const auto document = pdfkit::test::open_document(builder.build());
  const pdfkit::syntax::Stream created(dictionary, std::vector<std::byte>{});
  const auto copy = pdfkit::writer::StreamCopier::copy(*document, created);
  CHECK(copy.dictionary.get("Filter").array()->size() == 2);
  CHECK(copy.dictionary.get("Length").integer() == 0);
}

TEST_CASE("a rewritten stream is Flate-compressed and its /Length is right") {
  Dictionary dictionary;
  dictionary.set("Length", Object::make_integer(999));
  dictionary.set("DL", Object::make_integer(999));
  dictionary.set("DecodeParms", Object::make_name("Junk"));
  dictionary.set("Type", Object::make_name("XObject"));
  const auto stream =
      pdfkit::writer::StreamCopier::compress(dictionary, pdfkit::platform::as_bytes(content()));
  CHECK(stream.dictionary().get("Filter").is_name("FlateDecode"));
  CHECK_FALSE(stream.dictionary().contains("DL"));
  CHECK_FALSE(stream.dictionary().contains("DecodeParms"));
  CHECK_FALSE(stream.dictionary().contains("Length"));
  CHECK(stream.dictionary().get("Type").is_name("XObject"));
  PdfBuilder builder;
  builder.begin_document();
  (void)builder.add_page("BT ET");
  const auto document = pdfkit::test::open_document(builder.build());
  const auto copy = pdfkit::writer::StreamCopier::copy(*document, stream);
  CHECK(copy.dictionary.get("Length").integer() ==
        static_cast<std::int64_t>(stream.owned_data().size()));
  const auto decoded = document->decode(stream);
  CHECK(std::string(reinterpret_cast<const char*>(decoded.bytes.data()), decoded.bytes.size()) ==
        std::string(content()));
}
