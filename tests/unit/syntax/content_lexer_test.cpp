// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::ContentLexer: operators and operands with byte ranges over
// hand-written streams, a /Contents array joined with an operator split
// across two pieces and mapped back, inline images with "EI" inside the data
// with and without a computable length, an ASCII-filtered inline image, and
// the operator limit.

#include "syntax/content_lexer.hpp"

#include "platform/error.hpp"
#include "support/content_builder.hpp"
#include "support/require_value.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/limits.hpp"

#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::syntax::ContentLexer;
using pdfkit::syntax::Limits;
using pdfkit::test::ContentBuilder;
using pdfkit::test::require_value;

std::vector<std::byte> bytes_of(const std::string& text) {
  std::vector<std::byte> out;
  for (const char value : text) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

std::vector<ContentLexer::Operation> lex(const std::vector<std::string>& pieces,
                                         const Limits& limits = {}) {
  std::vector<std::vector<std::byte>> data;
  data.reserve(pieces.size());
  for (const std::string& piece : pieces) {
    data.push_back(bytes_of(piece));
  }
  ContentLexer lexer(data, limits, "page 1");
  std::vector<ContentLexer::Operation> operations;
  while (std::optional<ContentLexer::Operation> operation = lexer.next()) {
    operations.push_back(std::move(*operation));
  }
  return operations;
}

std::string data_of(const std::string& text, const ContentLexer::Operation& operation) {
  return text.substr(operation.data_start, operation.data_end - operation.data_start);
}

TEST_CASE("operators with their operands and byte ranges") {
  ContentBuilder content;
  content.save().matrix(1, 0, 0, 1, 72, 700).begin_text().font("F1", 12).show("Hi (there)");
  content.show_array({std::string("A"), -120.0, std::string("B")}).end_text().restore();
  const std::string text = content.text();
  const auto operations = lex({text});
  REQUIRE(operations.size() == 8U);
  CHECK(operations[0].op == "q");
  CHECK(operations[1].op == "cm");
  REQUIRE(operations[1].operands.size() == 6U);
  CHECK(operations[1].operands[4].number() == doctest::Approx(72));
  CHECK(text.substr(operations[1].start, operations[1].end - operations[1].start) ==
        "1 0 0 1 72 700 cm");
  CHECK(text.substr(operations[1].operator_start, 2) == "cm");
  CHECK(operations[3].op == "Tf");
  CHECK(operations[3].operands[0].is_name("F1"));
  CHECK(operations[4].op == "Tj");
  CHECK(operations[4].operands[0].string()->bytes == "Hi (there)");
  CHECK(operations[5].op == "TJ");
  CHECK(operations[5].operands[0].array()->size() == 3U);
  CHECK(operations[7].op == "Q");
}

TEST_CASE("operands use the object syntax without references") {
  const auto operations = lex({"1 0 R /P << /MCID 0 >> BDC true false null EMC"});
  REQUIRE(operations.size() == 3U);
  CHECK(operations[0].op == "R");
  CHECK(operations[0].operands.size() == 2U);
  CHECK(operations[1].op == "BDC");
  CHECK(operations[1].operands[1].dictionary()->get("MCID").integer() == 0);
  CHECK(operations[2].op == "EMC");
  CHECK(operations[2].operands.size() == 3U);
}

TEST_CASE("a /Contents array is joined with one newline and mapped back to its pieces") {
  std::vector<std::vector<std::byte>> pieces{bytes_of("BT /F1 12"), bytes_of("Tf ET")};
  const Limits limits;
  ContentLexer lexer(pieces, limits, "page 1");
  const auto bt = require_value(lexer.next());
  const auto tf = require_value(lexer.next());
  const auto et = require_value(lexer.next());
  CHECK(tf.op == "Tf");
  CHECK(tf.operands.size() == 2U);
  const ContentLexer::Location operand = lexer.locate(tf.start);
  CHECK(operand.piece == 0U);
  CHECK(operand.offset == 3U);
  const ContentLexer::Location op = lexer.locate(tf.operator_start);
  CHECK(op.piece == 1U);
  CHECK(op.offset == 0U);
  CHECK(lexer.locate(et.operator_start).offset == 3U);
  CHECK(bt.op == "BT");
  CHECK_FALSE(lexer.next().has_value());
}

TEST_CASE("an inline image whose computable length holds EI in its data") {
  // 4 by 1 pixels of 8-bit grey: 4 bytes, the middle two being "EI".
  ContentBuilder content;
  content.save().inline_image("/W 4 /H 1 /BPC 8 /CS /G", std::string("\x01"
                                                                     "EI\x02",
                                                                     4));
  content.restore();
  const std::string text = content.text();
  const auto operations = lex({text});
  REQUIRE(operations.size() == 3U);
  const ContentLexer::Operation& image = operations[1];
  CHECK(image.op == "BI");
  CHECK(image.operands[0].dictionary()->get("W").integer() == 4);
  CHECK(data_of(text, image) == std::string("\x01"
                                            "EI\x02",
                                            4));
  CHECK(text.substr(image.end - 2, 2) == "EI");
  CHECK(operations[2].op == "Q");
}

TEST_CASE("without a computable length, a false EI inside the data is passed over") {
  // A DCT image has no computable length; " EI " followed by bytes that do not
  // lex as operators is data.
  const std::string data = std::string("\xFF\xD8 EI \xC3\xA9\xFE\xFF", 10) + " more";
  const std::string text = "q BI /W 2 /H 2 /BPC 8 /CS /RGB /F /DCT ID " + data + "\nEI Q";
  const auto operations = lex({text});
  REQUIRE(operations.size() == 3U);
  CHECK(data_of(text, operations[1]) == data);
  CHECK(operations[2].op == "Q");
}

TEST_CASE("an ASCIIHex inline image ends at its marker") {
  const std::string text = "BI /W 2 /H 1 /BPC 8 /CS /G /F /AHx ID 0aEI>\nEI Q";
  const auto operations = lex({text});
  REQUIRE(operations.size() == 2U);
  CHECK(data_of(text, operations[0]) == "0aEI>");
}

TEST_CASE("an inline image without EI runs to the end and is a fault") {
  std::vector<std::vector<std::byte>> pieces{bytes_of("BI /W 1 /H 1 ID \x01\x02\x03")};
  const Limits limits;
  ContentLexer lexer(pieces, limits, "page 1");
  const auto image = require_value(lexer.next());
  CHECK(image.data_end == pieces[0].size());
  CHECK(lexer.faults() >= 1U);
}

TEST_CASE("past the operator limit the content is refused") {
  Limits limits;
  limits.operators_per_page = 3;
  try {
    (void)lex({"q q q q Q Q Q Q"}, limits);
    FAIL("the operator limit was not enforced");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
    CHECK(error.failed().find("page 1") != std::string::npos);
    CHECK(error.fix().find("operators_per_page") != std::string::npos);
  }
}

TEST_CASE("the operator table") {
  CHECK(ContentLexer::is_operator("Tj"));
  CHECK(ContentLexer::is_operator("\""));
  CHECK(ContentLexer::is_operator("scn"));
  CHECK_FALSE(ContentLexer::is_operator("Tx"));
}

}  // namespace
