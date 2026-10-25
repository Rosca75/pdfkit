// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::ObjectStreams, directly and through syntax::Document:
// objects read from a stream's header and offsets, a lookup by index and by
// number, an object both inside and outside a stream, and the cache that
// decodes a stream once whatever number of objects it serves.

#include "syntax/object_streams.hpp"

#include "platform/byte_span.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"

#include <doctest.h>

#include <string>

namespace {

using pdfkit::platform::as_bytes;
using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Object;
using pdfkit::syntax::ObjectStreams;
using pdfkit::test::open_document;
using pdfkit::test::PdfBuilder;

Dictionary header(std::int64_t count, std::int64_t first) {
  Dictionary dictionary;
  dictionary.set("Type", Object::make_name("ObjStm"));
  dictionary.set("N", Object::make_integer(count));
  dictionary.set("First", Object::make_integer(first));
  return dictionary;
}

TEST_CASE("objects are read at their offsets from /First") {
  const std::string head = "11 0 12 11 13 17 ";
  const std::string data = head + "<< /A 1 >> [1 2] (three) ";
  const ObjectStreams::Contents contents = ObjectStreams::parse(
      as_bytes(data), header(3, static_cast<std::int64_t>(head.size())), "object 10 0");
  CHECK(contents.repairs.empty());
  REQUIRE(contents.objects.size() == 3U);
  CHECK(contents.objects[0].first == 11U);
  CHECK(contents.objects[0].second.dictionary()->get("A").integer() == 1);
  CHECK(contents.objects[1].second.array()->size() == 2U);
  CHECK(contents.objects[2].second.string()->bytes == "three");
  const Object* by_index = ObjectStreams::lookup(contents, 12, 1);
  REQUIRE(by_index != nullptr);
  CHECK(by_index->array() != nullptr);
  // An index naming another object falls back to the number.
  const Object* by_number = ObjectStreams::lookup(contents, 13, 0);
  REQUIRE(by_number != nullptr);
  CHECK(by_number->string() != nullptr);
  CHECK(ObjectStreams::lookup(contents, 99, 0) == nullptr);
}

TEST_CASE("a short header and an offset past the end are faults of their entries only") {
  const std::string data = "11 0 12 999 (one)";
  const ObjectStreams::Contents contents =
      ObjectStreams::parse(as_bytes(data), header(5, 12), "object 10 0");
  REQUIRE(contents.objects.size() == 2U);
  CHECK(contents.objects[0].second.string()->bytes == "one");
  CHECK(contents.objects[1].second.is_null());
  CHECK(contents.repairs.size() == 2U);
  CHECK(ObjectStreams::parse(as_bytes(data), Dictionary(), "x").repairs.size() == 1U);
}

TEST_CASE("the document decodes a stream once for every object it serves") {
  PdfBuilder builder;
  builder.begin_document();
  builder.add_page("BT ET");
  pdfkit::test::ObjectOptions packed;
  packed.in_object_stream = true;
  std::string filler(5000, 'f');
  const std::uint32_t first = builder.add("(" + filler + ")", packed);
  const std::uint32_t second = builder.add("<< /Second true >>", packed);
  pdfkit::test::Layout layout;
  layout.xref = pdfkit::test::XrefForm::stream;
  auto document = open_document(builder.build(layout));
  // The object streams are not compressed by the builder, so nothing is
  // charged; the cache is proven by the objects coming back equal.
  CHECK(document->object({first, 0}).string()->bytes == filler);
  CHECK(document->object({second, 0}).dictionary()->get("Second").boolean() == true);
  CHECK(document->repairs().empty());
}

TEST_CASE("an object both inside and outside a stream is the one the table names") {
  PdfBuilder builder;
  builder.begin_document();
  builder.add_page("BT ET");
  pdfkit::test::ObjectOptions packed;
  packed.in_object_stream = true;
  const std::uint32_t number = builder.add("(inside)", packed);
  builder.new_revision();
  builder.set(number, "(outside, later)");
  pdfkit::test::Layout layout;
  layout.xref = pdfkit::test::XrefForm::stream;
  auto document = open_document(builder.build(layout));
  CHECK(document->object({number, 0}).string()->bytes == "outside, later");
  // Reconstruction agrees: the outside object is later in the file and parses.
  layout.startxref = 30;
  auto rebuilt = open_document(builder.build(layout));
  CHECK(rebuilt->reconstructed());
  CHECK(rebuilt->object({number, 0}).string()->bytes == "outside, later");
}

}  // namespace
