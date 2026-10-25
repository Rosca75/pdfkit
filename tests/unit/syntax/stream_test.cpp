// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the created form of a stream: it holds its own encoded bytes, the
// document hands them back as the raw data and decodes them through the
// filters without decrypting them, and a copy under another dictionary keeps
// the data and the owner.

#include "syntax/stream.hpp"

#include "platform/byte_span.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "support/pdf_encryptor.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <doctest.h>

#include <cstddef>
#include <string>
#include <vector>

namespace {

using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::syntax::Stream;

std::vector<std::byte> bytes_of(const std::string& text) {
  const auto view = pdfkit::platform::as_bytes(text);
  return {view.begin(), view.end()};
}

std::string text_of(pdfkit::platform::ByteSpan bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace

TEST_CASE("a created stream holds its bytes and is decoded without decryption") {
  pdfkit::test::PdfBuilder builder;
  builder.begin_document();
  (void)builder.add_page("BT ET");
  const pdfkit::test::PdfEncryptor::Spec spec;
  builder.encrypt(spec);
  const auto document = pdfkit::test::open_document(builder.build());
  REQUIRE(document->security().has_value());

  Dictionary dictionary;
  dictionary.set("Filter", Object::make_name("FlateDecode"));
  const Stream created(dictionary,
                       bytes_of(pdfkit::test::PdfBuilder::deflate("q 1 0 0 1 0 0 cm Q")));
  CHECK(created.owns_data());
  CHECK(created.describe() == "a created stream");
  CHECK(text_of(document->raw(created)) == text_of(created.owned_data()));
  const auto decoded = document->decode(created);
  CHECK_FALSE(decoded.damaged);
  CHECK(text_of(decoded.bytes) == "q 1 0 0 1 0 0 cm Q");

  Dictionary other;
  other.set("Type", Object::make_name("XObject"));
  const Stream copy = created.with_dictionary(other);
  CHECK(copy.owns_data());
  CHECK(text_of(copy.owned_data()) == text_of(created.owned_data()));
  CHECK_FALSE(copy == created);
  CHECK(copy == created.with_dictionary(other));
}

TEST_CASE("a stream read from a file owns no data") {
  const Stream read(Dictionary(), 10, 20, Object::Reference{4, 0});
  CHECK_FALSE(read.owns_data());
  CHECK(read.owned_data().empty());
  CHECK(read.describe() == "object 4 0");
  CHECK_FALSE(read == Stream(Dictionary(), std::vector<std::byte>{}));
}
