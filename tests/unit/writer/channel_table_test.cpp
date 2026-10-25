// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The planted-marker test of every row of the channel table: the builder
// plants a marker in one channel alone, pdfkit_channels reports the channel
// present with a count of one, pdfkit_write produces bytes in which the
// marker occurs neither raw nor in any decoded stream, and the output opens
// without a repair and draws its page. The table's row count is held to the
// count of rows tested here, so a row added without its test fails. The
// allow-lists of the catalog and the pages are tested on keys no row names.

#include "writer/channel_table.hpp"

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "support/document_summary.hpp"
#include "support/pdf_builder.hpp"
#include "support/pdf_builder_channels.hpp"
#include "syntax/dictionary.hpp"
#include "writer/output_scan.hpp"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;
using pdfkit::test::PdfBuilderChannels;

// The channels report of file.
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
  INFO(text);
  REQUIRE(status == PDFKIT_OK);
  return JsonReader::parse(text);
}

std::int64_t count_of(const JsonValue& report, std::string_view id) {
  for (const JsonValue& row : report.find("channels")->as_array()) {
    if (row.find("id")->as_string() == id) {
      return row.find("count")->as_int64().value_or(-1);
    }
  }
  FAIL("no row " << id);
  return -1;
}

std::int64_t stripped_of(const JsonValue& report, std::string_view id) {
  for (const JsonValue& row : report.find("channels")->as_array()) {
    if (row.find("id")->as_string() == id) {
      return row.find("stripped")->as_int64().value_or(-1);
    }
  }
  return -1;
}

// Plants a marker in row id, checks it is reported once, written nowhere,
// and that the output opens and draws the fixture's page.
void check_row(std::string_view id) {
  const auto& rows = PdfBuilderChannels::rows();
  const auto position = std::ranges::find(rows, id) - rows.begin();
  const std::string marker = PdfBuilderChannels::marker(static_cast<int>(position + 1), 1);
  const std::string file = PdfBuilderChannels::planted(id, marker);
  INFO("row " << id << ", marker " << marker);
  REQUIRE(pdfkit::test::contains_marker(file, marker));
  const JsonValue report = channels_of(file);
  CHECK(count_of(report, id) == 1);
  const std::string output = pdfkit::test::identity_write(file);
  CHECK_FALSE(pdfkit::test::contains_marker(output, marker));
  const JsonValue written = JsonReader::parse(pdfkit::test::write_report(file));
  CHECK(stripped_of(written, id) == 1);
  const auto document = pdfkit::test::reopen(output);
  REQUIRE(document->pages().size() == 1);
  CHECK(pdfkit::test::page_text(*document, 1).find(PdfBuilderChannels::drawn_text) !=
        std::string::npos);
  // The output carries no channel of the table any more, except the minimal
  // optional content that keeps hidden layers hidden.
  const JsonValue again = channels_of(output);
  for (const JsonValue& present : again.find("present")->as_array()) {
    CHECK(present.as_string() == "ch_optional_content");
  }
}

}  // namespace

TEST_CASE("the table holds one row per tested channel, in the order of DESIGN.md section 15") {
  const auto rows = pdfkit::writer::ChannelTable::rows();
  REQUIRE(rows.size() == PdfBuilderChannels::rows().size());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    CHECK(rows[i].id == PdfBuilderChannels::rows()[i]);
    CHECK_FALSE(rows[i].name.empty());
  }
}

TEST_CASE("channel ch_revisions") {
  check_row("ch_revisions");
}
TEST_CASE("channel ch_unreferenced") {
  check_row("ch_unreferenced");
}
TEST_CASE("channel ch_thumbnail") {
  check_row("ch_thumbnail");
}
TEST_CASE("channel ch_pieceinfo") {
  check_row("ch_pieceinfo");
}
TEST_CASE("channel ch_last_modified") {
  check_row("ch_last_modified");
}
TEST_CASE("channel ch_object_metadata") {
  check_row("ch_object_metadata");
}
TEST_CASE("channel ch_javascript") {
  check_row("ch_javascript");
}
TEST_CASE("channel ch_embedded_file") {
  check_row("ch_embedded_file");
}
TEST_CASE("channel ch_annotation") {
  check_row("ch_annotation");
}
TEST_CASE("channel ch_acroform") {
  check_row("ch_acroform");
}
TEST_CASE("channel ch_outline") {
  check_row("ch_outline");
}
TEST_CASE("channel ch_named_dest") {
  check_row("ch_named_dest");
}
TEST_CASE("channel ch_struct_tree") {
  check_row("ch_struct_tree");
}
TEST_CASE("channel ch_optional_content") {
  check_row("ch_optional_content");
}
TEST_CASE("channel ch_page_labels") {
  check_row("ch_page_labels");
}
TEST_CASE("channel ch_xmp_history") {
  check_row("ch_xmp_history");
}
TEST_CASE("channel ch_threads") {
  check_row("ch_threads");
}
TEST_CASE("channel ch_web_capture") {
  check_row("ch_web_capture");
}
TEST_CASE("channel ch_portfolio") {
  check_row("ch_portfolio");
}
TEST_CASE("channel ch_perms_legal") {
  check_row("ch_perms_legal");
}
TEST_CASE("channel ch_encrypt") {
  check_row("ch_encrypt");
}
TEST_CASE("channel ch_uri_base") {
  check_row("ch_uri_base");
}
TEST_CASE("channel ch_search_index") {
  check_row("ch_search_index");
}
TEST_CASE("channel ch_xobject_private") {
  check_row("ch_xobject_private");
}

TEST_CASE("every row above has its test case") {
  // One TEST_CASE per row: the list here is the rows tested above, so a row
  // added to the table without a test case of its own fails this count.
  constexpr std::array<std::string_view, 24> tested{
      "ch_revisions",     "ch_unreferenced",     "ch_thumbnail",    "ch_pieceinfo",
      "ch_last_modified", "ch_object_metadata",  "ch_javascript",   "ch_embedded_file",
      "ch_annotation",    "ch_acroform",         "ch_outline",      "ch_named_dest",
      "ch_struct_tree",   "ch_optional_content", "ch_page_labels",  "ch_xmp_history",
      "ch_threads",       "ch_web_capture",      "ch_portfolio",    "ch_perms_legal",
      "ch_encrypt",       "ch_uri_base",         "ch_search_index", "ch_xobject_private"};
  CHECK(pdfkit::writer::ChannelTable::rows().size() == tested.size());
}

TEST_CASE("keys no row names are dropped by the catalog and page allow-lists") {
  pdfkit::test::PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t page = builder.add_page(
      "BT ET", "/Rotate 90 /PrivateKey (PDFKITMARK99X01) /UserUnit 1 /SeparationInfo << >>");
  builder.set(builder.catalog(),
              "<< /Type /Catalog /Pages " + std::to_string(builder.page_tree()) +
                  " 0 R /Lang (en) /PageLayout /OneColumn /PageMode /UseOutlines /Vendor "
                  "(PDFKITMARK99X02) /ViewerPreferences << /FitWindow true /Private "
                  "(PDFKITMARK99X03) /NonFullScreenPageMode /UseThumbs >> >>");
  const std::string output = pdfkit::test::identity_write(builder.build());
  for (int n = 1; n <= 3; ++n) {
    CHECK_FALSE(pdfkit::test::contains_marker(output, PdfBuilderChannels::marker(99, n)));
  }
  const auto document = pdfkit::test::reopen(output);
  const auto catalog = document->catalog();
  const pdfkit::syntax::Dictionary& keys = *catalog.dictionary();
  CHECK(keys.get("Lang").string()->bytes == "en");
  CHECK(keys.get("PageLayout").is_name("OneColumn"));
  CHECK(keys.get("PageMode").is_name("UseNone"));
  const auto preferences = document->resolve_entry(keys, "ViewerPreferences");
  CHECK(preferences.dictionary()->get("FitWindow").boolean() == true);
  CHECK(preferences.dictionary()->get("NonFullScreenPageMode").is_name("UseNone"));
  const pdfkit::syntax::Dictionary& kept = document->pages().front().dictionary;
  CHECK(kept.get("Rotate").integer() == 90);
  CHECK(kept.get("UserUnit").integer() == 1);
  CHECK_FALSE(kept.contains("SeparationInfo"));
  (void)page;
}

TEST_CASE("a /Redact annotation is counted as a pending redaction with its warning") {
  pdfkit::test::PdfBuilder builder;
  builder.begin_document();
  const std::uint32_t annotation =
      builder.add("<< /Type /Annot /Subtype /Redact /Rect [0 0 10 10] /OverlayText (X) >>");
  (void)builder.add_page("BT ET");
  (void)builder.add_page("BT ET", "/Annots [" + std::to_string(annotation) + " 0 R]");
  const JsonValue report = channels_of(builder.build());
  CHECK(report.find("pending_redactions")->as_int64() == 1);
  const auto& warnings = report.find("warnings")->as_array();
  REQUIRE(warnings.size() == 1);
  CHECK(warnings.front().find("code")->as_string() == "pending_redactions");
  CHECK(warnings.front().find("page")->as_int64() == 2);
}

TEST_CASE("the XMP packet loses its media-management properties and keeps the rest") {
  const std::string marker = PdfBuilderChannels::marker(16, 2);
  const std::string output =
      pdfkit::test::identity_write(PdfBuilderChannels::planted("ch_xmp_history", marker));
  const std::string all = pdfkit::test::everything(output);
  CHECK(all.find("xmpMM:History") == std::string::npos);
  CHECK(all.find("xmpMM:DocumentID") == std::string::npos);
  CHECK(all.find("xmpMM:InstanceID") == std::string::npos);
  CHECK(all.find("uuid:1234") == std::string::npos);
  CHECK(all.find("An ordinary title") != std::string::npos);
  CHECK(all.find("<?xpacket end='w'?>") != std::string::npos);
  // The packet is written uncompressed, so a tool scanning the raw bytes
  // finds it.
  CHECK(output.find("An ordinary title") != std::string::npos);
}
