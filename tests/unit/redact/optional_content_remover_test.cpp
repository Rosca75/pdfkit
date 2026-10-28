// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the removal of optional content, docs/DESIGN.md section 15: over
// every kind of the off-layer fixture, the identity write holds no marker
// planted in content the default configuration turns off (raw or in a
// decoded stream), keeps every marker of content it shows, drawn where it
// was drawn, and holds no /OCProperties and no /OC name anywhere; the
// graphics state and the marked content of the page stay balanced; the
// channels report counts the groups and those turned off, and the write
// report counts the groups removed.

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "support/pdf_builder_channels.hpp"
#include "syntax/content_lexer.hpp"
#include "text/text_reader.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;
using pdfkit::test::PdfBuilderChannels;

std::string marker(int row, int n) {
  return PdfBuilderChannels::marker(row, n);
}

// Whether text names /OC (a key or a marked-content tag), but not a longer
// name such as /OCProperties.
bool names_oc(const std::string& text) {
  for (std::size_t at = text.find("/OC"); at != std::string::npos; at = text.find("/OC", at + 1)) {
    const std::size_t next = at + 3;
    if (next >= text.size()) {
      return true;
    }
    const auto c = static_cast<unsigned char>(text[next]);
    const bool regular = std::isalnum(c) != 0 || c == '_' || c == '.' || c == '#';
    if (!regular) {
      return true;
    }
  }
  return false;
}

// The counts of q, Q, the marked-content openers and EMC in page 1 of file.
struct Balance {
  std::size_t saves = 0;
  std::size_t restores = 0;
  std::size_t opened = 0;
  std::size_t closed = 0;
};

Balance balance(const std::string& file) {
  const auto document = pdfkit::test::reopen(file);
  const std::string content = pdfkit::test::page_text(*document, 1);
  const std::vector<std::vector<std::byte>> pieces{
      std::vector<std::byte>(reinterpret_cast<const std::byte*>(content.data()),
                             reinterpret_cast<const std::byte*>(content.data()) + content.size())};
  pdfkit::syntax::ContentLexer lexer(pieces, document->limits(), "page 1");
  Balance out;
  while (const auto operation = lexer.next()) {
    out.saves += operation->op == "q" ? 1U : 0U;
    out.restores += operation->op == "Q" ? 1U : 0U;
    out.opened += operation->op == "BDC" || operation->op == "BMC" ? 1U : 0U;
    out.closed += operation->op == "EMC" ? 1U : 0U;
  }
  return out;
}

// The device-space origin of the first glyph of the run text on page 1 of
// file, by the interpreter; no value when the page does not show it.
std::optional<std::pair<double, double>> origin_of(const std::string& file,
                                                   const std::string& text) {
  const auto document = pdfkit::test::open_document(file);
  pdfkit::text::TextReader reader(*document);
  const pdfkit::text::PageContent content = reader.interpret(1);
  std::string joined;
  std::vector<std::size_t> owner;
  for (std::size_t i = 0; i < content.glyphs.size(); ++i) {
    joined += content.glyphs[i].text;
    owner.resize(joined.size(), i);
  }
  const std::size_t at = joined.find(text);
  if (at == std::string::npos) {
    return std::nullopt;
  }
  const auto& glyph = content.glyphs[owner[at]];
  return std::pair{glyph.origin.x, glyph.origin.y};
}

JsonValue channels_of(const std::string& file) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  REQUIRE(pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()), file.size(), nullptr,
                      &doc, &out) == PDFKIT_OK);
  pdfkit_buffer_free(&out);
  const pdfkit_status status = pdfkit_channels(doc, nullptr, &out);
  const std::string text(reinterpret_cast<const char*>(out.data), out.size);
  pdfkit_buffer_free(&out);
  pdfkit_close(doc);
  REQUIRE(status == PDFKIT_OK);
  return JsonReader::parse(text);
}

const JsonValue* row_of(const JsonValue& report, std::string_view id) {
  for (const JsonValue& row : report.find("channels")->as_array()) {
    if (row.find("id")->as_string() == id) {
      return &row;
    }
  }
  return nullptr;
}

// The markers kind plants in content shown (row 90) and hidden (row 25).
std::vector<std::string> planted(std::string_view kind, int row) {
  const std::string file = PdfBuilderChannels::off_layers(kind);
  std::vector<std::string> out;
  for (int n = 1; n <= 9; ++n) {
    if (pdfkit::test::contains_marker(file, marker(row, n))) {
      out.push_back(marker(row, n));
    }
  }
  return out;
}

}  // namespace

TEST_CASE("content in a group turned off leaves every output, content in a group on stays") {
  for (const std::string_view kind : PdfBuilderChannels::off_layer_kinds) {
    INFO("kind " << kind);
    const std::string file = PdfBuilderChannels::off_layers(kind);
    const std::vector<std::string> hidden = planted(kind, 25);
    const std::vector<std::string> shown = planted(kind, 90);
    REQUIRE_FALSE(hidden.empty());
    REQUIRE_FALSE(shown.empty());
    const std::string output = pdfkit::test::identity_write(file);
    for (const std::string& text : hidden) {
      CHECK_FALSE(pdfkit::test::contains_marker(output, text));
    }
    for (const std::string& text : shown) {
      CHECK(pdfkit::test::contains_marker(output, text));
      // A run the page does not show reads as a point no glyph has.
      constexpr std::pair<double, double> nowhere{-1.0e9, -1.0e9};
      const auto before = origin_of(file, text).value_or(nowhere);
      const auto after = origin_of(output, text).value_or(nowhere);
      CHECK(before.first > nowhere.first);
      CHECK(std::abs(before.first - after.first) < 0.01);
      CHECK(std::abs(before.second - after.second) < 0.01);
    }
    CHECK(origin_of(output, std::string(PdfBuilderChannels::drawn_text)).has_value());
    // The groups' names go with /OCProperties, and no /OC remains.
    CHECK_FALSE(pdfkit::test::contains_marker(output, marker(14, 1)));
    CHECK_FALSE(pdfkit::test::contains_marker(output, marker(14, 2)));
    const std::string all = pdfkit::test::everything(output);
    CHECK(all.find("/OCProperties") == std::string::npos);
    CHECK_FALSE(names_oc(all));
    CHECK(all.find("/UseOC") == std::string::npos);
    const Balance kept = balance(output);
    CHECK(kept.saves == kept.restores);
    CHECK(kept.opened == kept.closed);
    const Balance source = balance(file);
    CHECK(kept.saves == source.saves);
  }
}

TEST_CASE("the channels report counts the groups and those turned off") {
  for (const std::string_view kind : PdfBuilderChannels::off_layer_kinds) {
    INFO("kind " << kind);
    const std::string file = PdfBuilderChannels::off_layers(kind);
    const JsonValue report = channels_of(file);
    const JsonValue* row = row_of(report, "ch_optional_content");
    REQUIRE(row != nullptr);
    CHECK(row->find("count")->as_int64() == 2);
    REQUIRE(row->find("off_groups") != nullptr);
    CHECK(row->find("off_groups")->as_int64() == 1);
    const JsonValue written = JsonReader::parse(pdfkit::test::write_report(file));
    const JsonValue* stripped = row_of(written, "ch_optional_content");
    REQUIRE(stripped != nullptr);
    CHECK(stripped->find("stripped")->as_int64() == 2);
    const JsonValue again = channels_of(pdfkit::test::identity_write(file));
    CHECK(row_of(again, "ch_optional_content")->find("count")->as_int64() == 0);
    CHECK(row_of(again, "ch_optional_content")->find("off_groups")->as_int64() == 0);
  }
}

TEST_CASE("without /OCProperties every group is on: the content stays and the marks go") {
  pdfkit::test::PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t font = builder.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  const std::uint32_t group =
      builder.add("<< /Type /OCG /Name " + std::string("(") + marker(14, 1) + ") >>");
  (void)builder.add_page("/OC /g BDC BT /F1 12 Tf 72 700 Td (" + marker(90, 1) + ") Tj ET EMC",
                         "/Resources << /Font << /F1 " + std::to_string(font) +
                             " 0 R >> /Properties << /g " + std::to_string(group) + " 0 R >> >>");
  const std::string file = builder.build();
  const std::string output = pdfkit::test::identity_write(file);
  CHECK(pdfkit::test::contains_marker(output, marker(90, 1)));
  CHECK_FALSE(pdfkit::test::contains_marker(output, marker(14, 1)));
  CHECK_FALSE(names_oc(pdfkit::test::everything(output)));
  const Balance kept = balance(output);
  CHECK(kept.opened == 0);
  CHECK(kept.closed == 0);
}

TEST_CASE("a page selection removes off content from the pages it keeps") {
  const std::string file = PdfBuilderChannels::off_layers("form");
  const std::string output = pdfkit::test::select_write(file, {1});
  CHECK_FALSE(pdfkit::test::contains_marker(output, marker(25, 1)));
  CHECK_FALSE(pdfkit::test::contains_marker(output, marker(25, 2)));
  CHECK(pdfkit::test::contains_marker(output, marker(90, 1)));
}
