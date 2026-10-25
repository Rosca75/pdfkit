// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::Filters: each filter over known bytes, the PNG and TIFF
// predictors, a chain, a damaged Flate stream yielding its prefix, a budget
// refusal naming the object, the limit and the way out, and an image codec
// handed back encoded.

#include "syntax/filters.hpp"

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "platform/error.hpp"
#include "support/pdf_builder.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::platform::as_bytes;
using pdfkit::platform::Budget;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::syntax::Dictionary;
using pdfkit::syntax::Filters;
using pdfkit::syntax::Object;
using pdfkit::test::PdfBuilder;

std::string text(const std::vector<std::byte>& bytes) {
  std::string out;
  for (const std::byte value : bytes) {
    out.push_back(static_cast<char>(value));
  }
  return out;
}

Filters::Result run(const std::string& data, const Object& filter,
                    const Object& parameters = Object()) {
  Budget budget;
  return Filters::decode(as_bytes(data), filter, parameters, budget, "object 9 0");
}

Filters::Result run(const std::string& data, const std::string& name,
                    const Object& parameters = Object()) {
  return run(data, Object::make_name(name), parameters);
}

Object predictor(int predictor_value, int colors, int bits, int columns) {
  Dictionary parameters;
  parameters.set("Predictor", Object::make_integer(predictor_value));
  parameters.set("Colors", Object::make_integer(colors));
  parameters.set("BitsPerComponent", Object::make_integer(bits));
  parameters.set("Columns", Object::make_integer(columns));
  return Object::make_dictionary(parameters);
}

std::string bytes_of(std::initializer_list<int> values) {
  std::string out;
  for (const int value : values) {
    out.push_back(static_cast<char>(value));
  }
  return out;
}

std::string sample(std::size_t size) {
  // Letters from a linear congruential sequence, so the compressed position
  // of a byte is roughly proportional to its decoded position.
  std::string out;
  std::uint32_t state = 12345;
  for (std::size_t i = 0; i < size; ++i) {
    state = state * 1103515245U + 12345U;
    out.push_back(static_cast<char>('a' + (state >> 16U) % 26));
  }
  return out;
}

TEST_CASE("no filter hands the data back") {
  const Filters::Result result = run("abc", Object());
  CHECK(text(result.bytes) == "abc");
  CHECK_FALSE(result.damaged);
  CHECK(result.remaining.empty());
}

TEST_CASE("FlateDecode with a zlib header and without one") {
  const std::string original = sample(5000);
  const std::string zlib = PdfBuilder::deflate(original);
  CHECK(text(run(zlib, "FlateDecode").bytes) == original);
  // A raw deflate stream: the zlib stream less its two-byte header and Adler-32.
  const std::string raw = zlib.substr(2, zlib.size() - 6);
  const Filters::Result result = run(raw, "FlateDecode");
  CHECK(text(result.bytes) == original);
  CHECK_FALSE(result.damaged);
  // A missing Adler-32 is no damage: viewers do not check it.
  CHECK_FALSE(run(zlib.substr(0, zlib.size() - 4), "FlateDecode").damaged);
}

TEST_CASE("a damaged Flate stream yields what inflated before the damage") {
  const std::string original = sample(20000);
  std::string zlib = PdfBuilder::deflate(original);
  for (std::size_t i = zlib.size() / 2; i < zlib.size() / 2 + 12; ++i) {
    zlib[i] = static_cast<char>(static_cast<unsigned char>(zlib[i]) ^ 0x5AU);
  }
  const Filters::Result result = run(zlib, "FlateDecode");
  CHECK(result.damaged);
  CHECK_FALSE(result.fault.empty());
  // The bytes before the damage decode as they were written.
  CHECK(original.substr(0, 4000) == text(result.bytes).substr(0, 4000));
}

TEST_CASE("a truncated Flate stream is damaged and keeps its prefix") {
  const std::string original = sample(20000);
  const std::string zlib = PdfBuilder::deflate(original);
  const Filters::Result result = run(zlib.substr(0, zlib.size() / 2), "FlateDecode");
  CHECK(result.damaged);
  CHECK(original.starts_with(text(result.bytes)));
  CHECK_FALSE(result.bytes.empty());
}

TEST_CASE("LZWDecode reads the specification's example") {
  // ISO 32000-1 section 7.4.4.2, the example "-----A---B" (recalled).
  const std::string encoded = bytes_of({0x80, 0x0B, 0x60, 0x50, 0x22, 0x0C, 0x0C, 0x85, 0x01});
  CHECK(text(run(encoded, "LZWDecode").bytes) == "-----A---B");
  CHECK(text(run(encoded, "LZW").bytes) == "-----A---B");
}

TEST_CASE("ASCIIHexDecode skips white space and pads an odd final digit") {
  CHECK(text(run("48 65 6c\n6C 6F>", "ASCIIHexDecode").bytes) == "Hello");
  CHECK(text(run("487>", "ASCIIHexDecode").bytes) == "Hp");
  CHECK(text(run("4142", "AHx").bytes) == "AB");
  const Filters::Result bad = run("41zz42>", "ASCIIHexDecode");
  CHECK(bad.damaged);
  CHECK(text(bad.bytes) == "A");
}

TEST_CASE("ASCII85Decode with z, white space, the end marker and a partial group") {
  // Expected bytes from Python's base64.a85encode (computed).
  CHECK(text(run("87cURD]i,\"Ebo7~>", "ASCII85Decode").bytes) == "Hello World");
  CHECK(text(run("87cUR D]i,\n\"Ebo7~>", "ASCII85Decode").bytes) == "Hello World");
  CHECK(text(run("z@:B~>", "A85").bytes) == std::string("\0\0\0\0ab", 6));
  CHECK(text(run("87cURD]i,\"Ebo7", "ASCII85Decode").bytes) == "Hello World");
  CHECK(run("87cU{RD~>", "ASCII85Decode").damaged);
}

TEST_CASE("RunLengthDecode copies, repeats and ends at 128") {
  const std::string encoded = bytes_of({2, 'a', 'b', 'c', 254, 'x', 128, 'z', 'z'});
  CHECK(text(run(encoded, "RunLengthDecode").bytes) == "abcxxx");
  CHECK(run(bytes_of({5, 'a'}), "RL").damaged);
}

TEST_CASE("the PNG predictors None, Sub, Up, Average and Paeth") {
  // Rows [1 2 3] Sub, [4 5 6] Up, [10 20 30] Average, [7 7 7] Paeth, [9 9 9] None,
  // encoded by hand from the PNG specification's filter definitions (recalled).
  const std::string encoded =
      bytes_of({1, 1, 1, 1, 2, 3, 3, 3, 3, 8, 13, 17, 4, 253, 243, 243, 0, 9, 9, 9});
  const Filters::Result result =
      run(PdfBuilder::deflate(encoded), "FlateDecode", predictor(12, 1, 8, 3));
  CHECK(text(result.bytes) == bytes_of({1, 2, 3, 4, 5, 6, 10, 20, 30, 7, 7, 7, 9, 9, 9}));
  CHECK_FALSE(result.damaged);
  // The filter type is read per row whatever the predictor number says.
  CHECK(text(run(PdfBuilder::deflate(encoded), "FlateDecode", predictor(10, 1, 8, 3)).bytes) ==
        bytes_of({1, 2, 3, 4, 5, 6, 10, 20, 30, 7, 7, 7, 9, 9, 9}));
}

TEST_CASE("the TIFF predictor at 8, 16 and 4 bits per component") {
  CHECK(text(run(PdfBuilder::deflate(bytes_of({1, 1, 1, 5, 1, 1})), "FlateDecode",
                 predictor(2, 1, 8, 3))
                 .bytes) == bytes_of({1, 2, 3, 5, 6, 7}));
  CHECK(text(run(PdfBuilder::deflate(bytes_of({1, 0, 0, 1})), "FlateDecode", predictor(2, 1, 16, 2))
                 .bytes) == bytes_of({1, 0, 1, 1}));
  CHECK(text(run(PdfBuilder::deflate(bytes_of({0x11, 0x11})), "FlateDecode", predictor(2, 1, 4, 4))
                 .bytes) == bytes_of({0x12, 0x34}));
  // Two colours of 8 bits: each component adds to the same component on its left.
  CHECK(text(run(PdfBuilder::deflate(bytes_of({1, 2, 1, 1})), "FlateDecode", predictor(2, 2, 8, 2))
                 .bytes) == bytes_of({1, 2, 2, 3}));
}

TEST_CASE("a chain is applied in the order /Filter lists it") {
  const std::string original = sample(300);
  std::string hex;
  for (const char value : PdfBuilder::deflate(original)) {
    static constexpr char digits[] = "0123456789abcdef";
    hex.push_back(digits[static_cast<unsigned char>(value) >> 4U]);
    hex.push_back(digits[static_cast<unsigned char>(value) & 0x0FU]);
  }
  hex += '>';
  const Object chain =
      Object::make_array({Object::make_name("ASCIIHexDecode"), Object::make_name("FlateDecode")});
  CHECK(text(run(hex, chain).bytes) == original);
}

TEST_CASE("an image codec stops the chain and is handed back encoded") {
  const std::string jpeg = "\xFF\xD8 not really a JPEG";
  const Object chain =
      Object::make_array({Object::make_name("FlateDecode"), Object::make_name("DCTDecode")});
  Dictionary dct;
  dct.set("ColorTransform", Object::make_integer(0));
  const Object parameters = Object::make_array({Object(), Object::make_dictionary(dct)});
  const Filters::Result result = run(PdfBuilder::deflate(jpeg), chain, parameters);
  CHECK(text(result.bytes) == jpeg);
  REQUIRE(result.remaining.size() == 1U);
  CHECK(result.remaining[0] == "DCTDecode");
  REQUIRE(result.remaining_parameters.size() == 1U);
  CHECK(result.remaining_parameters[0].dictionary()->get("ColorTransform").integer() == 0);
  CHECK_FALSE(result.damaged);
  CHECK(Filters::is_image_codec("JBIG2Decode"));
  CHECK(Filters::full_name("CCF") == "CCITTFaxDecode");
  CHECK(Filters::full_name("DCT") == "DCTDecode");
}

TEST_CASE("an unknown filter stops the chain") {
  const Filters::Result result = run("abc", "NoSuchDecode");
  CHECK(text(result.bytes) == "abc");
  REQUIRE(result.remaining.size() == 1U);
  CHECK(result.remaining[0] == "NoSuchDecode");
}

TEST_CASE("a stream past the per-stream budget is refused naming the object and the way out") {
  Budget budget(Budget::Limits{4096, 1ULL << 30U, 1000});
  const std::string zlib = PdfBuilder::deflate(std::string(10000, '\0'));
  try {
    (void)Filters::decode(as_bytes(zlib), Object::make_name("FlateDecode"), Object(), budget,
                          "object 9 0");
    FAIL("the decode was not refused");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
    CHECK(error.failed().find("object 9 0") != std::string::npos);
    CHECK(error.expected().find("4096") != std::string::npos);
    CHECK(error.fix().find("stream_bytes") != std::string::npos);
  }
}

}  // namespace
