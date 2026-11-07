// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The damaged pages through the C ABI (docs/ABI.md, the damaged_pages field;
// docs/DESIGN.md, the damaged pages section): every output a caller reads
// (inspect, text, find, check and the write report) names each page whose
// text could not be read in full, with the stream and the damage class, so a
// caller never takes partial text for the whole page. One fixture per class:
// each filter fault, content the lexer skipped, an operator it does not
// know, an inline image it could not end; and the damage that names no page:
// a wrong /Length, a missing endstream, a lexer recovery that loses nothing,
// an unknown operator inside BX and EX. A damaged form names every page that
// draws it; inspect answers the same before and after a text read; a limit
// met while inspect reads a page names the page as not checked.

#include "pdfkit/pdfkit.h"

#include "platform/json_reader.hpp"
#include "platform/json_value.hpp"
#include "support/pdf_builder.hpp"

#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using pdfkit::platform::JsonReader;
using pdfkit::platform::JsonValue;
using pdfkit::test::FlateDamage;
using pdfkit::test::PdfBuilder;
using pdfkit::test::StreamOptions;

// Two lines of text; the damage of a fixture starts between them, so the
// first line reads whole and the second is what the damage takes.
constexpr std::string_view hello_line = "BT /F1 12 Tf 72 700 Td (Hello) Tj ET\n";
constexpr std::string_view world_line = "BT /F1 12 Tf 72 680 Td (World) Tj ET\n";

std::string hello() {
  return std::string(hello_line);
}
std::string world() {
  return std::string(world_line);
}

std::string take(pdfkit_buffer& buffer) {
  std::string text(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  return text;
}

// A document of pages drawing in Helvetica, F1 in every page's resources.
class Fixture {
 public:
  Fixture() {
    builder_.begin_document();
    font_ = builder_.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  }

  // A page drawing content written under options, with further stream
  // entries and further resource entries.
  void page(const std::string& content, const StreamOptions& options = {},
            const std::string& stream_entries = "", const std::string& resources = "") {
    (void)builder_.add_page(
        content,
        "/Resources << /Font << /F1 " + std::to_string(font_) + " 0 R >> " + resources + " >>",
        options, stream_entries);
  }

  // A form drawing content written under options; answers its number.
  std::uint32_t form(const std::string& content, const StreamOptions& options = {}) {
    return builder_.add_stream(
        "/Type /XObject /Subtype /Form /BBox [0 0 612 792] /Resources << "
        "/Font << /F1 " +
            std::to_string(font_) + " 0 R >> >>",
        content, options);
  }

  [[nodiscard]] std::string bytes() const { return builder_.build(); }

 private:
  PdfBuilder builder_;
  std::uint32_t font_ = 0;
};

StreamOptions flate(FlateDamage damage, std::size_t at = hello().size()) {
  StreamOptions options;
  options.flate = true;
  options.flate_damage = damage;
  options.damage_at = at;
  return options;
}

// An open document, closed when it goes.
class Opened {
 public:
  explicit Opened(const std::string& file, const char* options = nullptr) {
    pdfkit_buffer out{nullptr, 0, nullptr};
    const pdfkit_status status = pdfkit_open(reinterpret_cast<const std::uint8_t*>(file.data()),
                                             file.size(), options, &document_, &out);
    const std::string report = take(out);
    INFO(report);
    REQUIRE(status == PDFKIT_OK);
  }
  Opened(const Opened&) = delete;
  Opened& operator=(const Opened&) = delete;
  Opened(Opened&&) = delete;
  Opened& operator=(Opened&&) = delete;
  ~Opened() { pdfkit_close(document_); }

  [[nodiscard]] pdfkit_document* get() const noexcept { return document_; }

 private:
  pdfkit_document* document_ = nullptr;
};

JsonValue parse(const std::string& text) {
  INFO(text);
  return JsonReader::parse(text);
}

JsonValue inspect(pdfkit_document* document) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_inspect(document, nullptr, &out);
  const std::string text = take(out);
  INFO(text);
  REQUIRE(status == PDFKIT_OK);
  return parse(text);
}

JsonValue text_of(pdfkit_document* document, const char* options = nullptr) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_text(document, options, &out);
  const std::string text = take(out);
  INFO(text);
  REQUIRE(status == PDFKIT_OK);
  return parse(text);
}

JsonValue find_of(pdfkit_document* document, const std::string& values) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_find(document, values.c_str(), &out);
  const std::string text = take(out);
  INFO(text);
  REQUIRE(status == PDFKIT_OK);
  return parse(text);
}

JsonValue check_of(const std::string& file, const std::string& strings) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_check(reinterpret_cast<const std::uint8_t*>(file.data()),
                                            file.size(), strings.c_str(), &out);
  const std::string text = take(out);
  INFO(text);
  REQUIRE(status == PDFKIT_OK);
  return parse(text);
}

// The write report of file with matches (none when empty) and the output.
std::pair<JsonValue, std::string> write_of(const std::string& file, const std::string& matches) {
  const Opened opened(file);
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_edit* edit = nullptr;
  REQUIRE(pdfkit_edit_new(opened.get(), &edit, &out) == PDFKIT_OK);
  (void)take(out);
  if (!matches.empty()) {
    const pdfkit_status status = pdfkit_redact_text(edit, matches.c_str(), &out);
    const std::string answer = take(out);
    INFO(answer);
    REQUIRE(status == PDFKIT_OK);
  }
  pdfkit_buffer pdf{nullptr, 0, nullptr};
  const pdfkit_status status = pdfkit_write(edit, nullptr, &pdf, &out);
  const std::string report = take(out);
  std::string output = take(pdf);
  pdfkit_edit_free(edit);
  INFO(report);
  REQUIRE(status == PDFKIT_OK);
  return {parse(report), std::move(output)};
}

// The damaged_pages member of an answer, which every output carries.
const JsonValue::Array& damaged(const JsonValue& answer) {
  const JsonValue* member = answer.find("damaged_pages");
  REQUIRE(member != nullptr);
  REQUIRE(member->kind() == JsonValue::Kind::array);
  return member->as_array();
}

// "page:class" for every stream of every damaged page, in order.
std::vector<std::string> named(const JsonValue& answer) {
  std::vector<std::string> out;
  for (const JsonValue& page : damaged(answer)) {
    for (const JsonValue& stream : page.find("streams")->as_array()) {
      out.push_back(std::to_string(page.find("page")->as_int64().value_or(0)) + ":" +
                    stream.find("class")->as_string());
    }
  }
  return out;
}

std::string keys_of(const JsonValue& object) {
  std::string out;
  for (const auto& member : object.as_object()) {
    out += member.first + " ";
  }
  return out;
}

// data in ASCII85 (ISO 32000-1 section 7.4.3), without the end marker.
std::string ascii85(const std::string& data) {
  std::string out;
  for (std::size_t at = 0; at < data.size(); at += 4) {
    const std::size_t count = std::min<std::size_t>(4, data.size() - at);
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      value = (value << 8U) | (i < count ? static_cast<unsigned char>(data[at + i]) : 0U);
    }
    std::string group(5, '!');
    for (std::size_t i = 5; i > 0; --i) {
      group[i - 1] = static_cast<char>('!' + value % 85U);
      value /= 85U;
    }
    out += group.substr(0, count + 1);
  }
  return out;
}

constexpr const char* search_hello = R"({"matches": [{"search": "Hello", "placeholder": "[X]"}]})";

// Every output names exactly the pages expected, with these classes.
void every_output_names(const std::string& file, const std::vector<std::string>& expected) {
  const Opened opened(file);
  CHECK(named(inspect(opened.get())) == expected);
  CHECK(named(text_of(opened.get())) == expected);
  CHECK(named(find_of(opened.get(), R"({"values": [{"text": "Hello"}]})")) == expected);
  CHECK(named(check_of(file, R"({"strings": [{"text": "Zebulon"}]})")) == expected);
  CHECK(named(check_of(file, R"({"strings": []})")) == expected);
  CHECK(named(write_of(file, "").first) == expected);
}

}  // namespace

TEST_CASE("a page read in full names no damage in any output") {
  Fixture fixture;
  fixture.page(hello() + world(), flate(FlateDamage::none));
  every_output_names(fixture.bytes(), {});
}

TEST_CASE("each Flate fault names its page with its class") {
  for (const auto& [damage, name] : {std::pair{FlateDamage::checksum, "flate_checksum"},
                                     std::pair{FlateDamage::corrupt, "flate_corrupt"},
                                     std::pair{FlateDamage::truncated, "flate_truncated"}}) {
    CAPTURE(name);
    Fixture fixture;
    fixture.page(hello() + world());
    fixture.page(hello() + world(), flate(damage));
    every_output_names(fixture.bytes(), {std::string("2:") + name});
  }
}

TEST_CASE("the damaged page's entry carries its stream, fault and bytes") {
  Fixture fixture;
  fixture.page(hello() + world(), flate(FlateDamage::truncated));
  const Opened opened(fixture.bytes());
  const JsonValue answer = inspect(opened.get());
  REQUIRE(damaged(answer).size() == 1);
  const JsonValue& page = damaged(answer).front();
  CHECK(keys_of(page) == "page glyphs streams ");
  CHECK(page.find("page")->as_int64() == 1);
  // The first line read before the cut: H, e, l, l, o.
  CHECK(page.find("glyphs")->as_int64() == 5);
  const JsonValue& stream = page.find("streams")->as_array().front();
  CHECK(keys_of(stream) == "stream class fault decoded_bytes unread_bytes ");
  CHECK(keys_of(*stream.find("stream")) == "piece form form_path ");
  CHECK(stream.find("stream")->find("piece")->as_int64() == 0);
  CHECK(stream.find("stream")->find("form")->kind() == JsonValue::Kind::null);
  CHECK(stream.find("stream")->find("form_path")->as_array().empty());
  CHECK(stream.find("fault")->as_string().find("ends before its last block") != std::string::npos);
  CHECK(stream.find("decoded_bytes")->as_int64() == static_cast<std::int64_t>(hello().size()));
  CHECK(stream.find("unread_bytes")->as_int64() == 0);
  // The key comes last, after the members the call wrote before.
  CHECK(keys_of(answer) == "version page_count pages encryption repairs warnings damaged_pages ");
}

TEST_CASE("a corrupt Flate stream counts the encoded bytes it left unread") {
  Fixture fixture;
  fixture.page(hello() + world(), flate(FlateDamage::corrupt));
  const Opened opened(fixture.bytes());
  const JsonValue answer = inspect(opened.get());
  REQUIRE(damaged(answer).size() == 1);
  const JsonValue& stream = damaged(answer).front().find("streams")->as_array().front();
  CHECK(stream.find("decoded_bytes")->as_int64() == static_cast<std::int64_t>(hello().size()));
  CHECK(stream.find("unread_bytes")->as_int64() > 0);
}

TEST_CASE("the other filters' faults name the page") {
  // ASCIIHex: the hexadecimal digits of the first line, then a byte that is
  // not one.
  std::string hex;
  for (const char c : hello()) {
    constexpr const char* digits = "0123456789ABCDEF";
    hex += digits[static_cast<unsigned char>(c) >> 4U];
    hex += digits[static_cast<unsigned char>(c) & 0x0FU];
  }
  // RunLength: one literal run of the first line, then a run that claims
  // more bytes than follow.
  std::string run_length(1, static_cast<char>(hello().size() - 1));
  run_length += hello() + std::string(1, static_cast<char>(40)) + "BT";
  const std::vector<std::tuple<std::string, std::string, std::string>> cases{
      {"/Filter /ASCIIHexDecode", hex + "ZZ" + ">", "ascii_hex"},
      {"/Filter /ASCII85Decode", ascii85(hello()) + "vx~>", "ascii85"},
      {"/Filter /LZWDecode", std::string("\x96\x00\x00", 3), "lzw"},
      {"/Filter /RunLengthDecode", run_length, "run_length"},
      {"/DecodeParms << /Predictor 3 >>", "", "predictor"}};
  for (const auto& [entries, data, name] : cases) {
    CAPTURE(name);
    Fixture fixture;
    if (name == "predictor") {
      // Flate with predictor parameters no predictor defines: the bytes
      // reach the lexer as they inflated.
      fixture.page(hello() + world(), flate(FlateDamage::none), entries);
    } else {
      fixture.page(data, {}, entries);
    }
    every_output_names(fixture.bytes(), {"1:" + name});
  }
}

TEST_CASE("content the lexer skips or an operator it drops names the page") {
  const std::vector<std::pair<std::string, std::string>> cases{
      // A keyword Annex A does not know, outside BX and EX.
      {hello() + "1 2 zork\n" + world(), "unknown_operator"},
      // An operator swallowed into an array operand.
      {hello() + "BT /F1 12 Tf 72 680 Td [(Wor) Tj (ld)] TJ ET\n", "unlexable_content"},
      // A string that runs to the end of the content.
      {hello() + "BT /F1 12 Tf 72 680 Td (World Tj ET\n", "unlexable_content"},
      // An inline image whose end is never found.
      {hello() + "BI /W 4 /H 4 /CS /G /BPC 8 /F /Fl ID xxxx\n" + world(), "inline_image"}};
  for (const auto& [content, name] : cases) {
    CAPTURE(name);
    Fixture fixture;
    fixture.page(content);
    every_output_names(fixture.bytes(), {"1:" + name});
  }
}

TEST_CASE("a recovery that loses nothing names no page") {
  const std::vector<std::string> cases{// Trailing operands at the end of the content.
                                       hello() + world() + "1 2 3",
                                       // A stray delimiter between operators.
                                       hello() + ") >> ] }\n" + world(),
                                       // An unknown operator inside a compatibility section.
                                       hello() + "BX 1 2 zork EX\n" + world()};
  for (const std::string& content : cases) {
    CAPTURE(content);
    Fixture fixture;
    fixture.page(content);
    every_output_names(fixture.bytes(), {});
  }
}

TEST_CASE("a stream the parser recovered whole names no page") {
  for (const bool omit_endstream : {false, true}) {
    CAPTURE(omit_endstream);
    Fixture fixture;
    StreamOptions options;
    if (omit_endstream) {
      options.omit_endstream = true;
    } else {
      options.length_delta = 7;
    }
    fixture.page(hello() + world(), options);
    every_output_names(fixture.bytes(), {});
    const Opened opened(fixture.bytes());
    CHECK_FALSE(inspect(opened.get()).find("repairs")->as_array().empty());
  }
}

TEST_CASE("a damaged form names every page that draws it, by its form path") {
  Fixture fixture;
  const std::uint32_t form = fixture.form(hello() + world(), flate(FlateDamage::checksum));
  const std::string resources = "/XObject << /X1 " + std::to_string(form) + " 0 R >>";
  fixture.page("/X1 Do\n", {}, "", resources);
  fixture.page(hello());
  fixture.page("q /X1 Do Q\n", {}, "", resources);
  const std::string file = fixture.bytes();
  every_output_names(file, {"1:flate_checksum", "3:flate_checksum"});
  const Opened opened(file);
  const JsonValue answer = text_of(opened.get());
  const JsonValue& stream = damaged(answer).back().find("streams")->as_array().front();
  CHECK(stream.find("stream")->find("piece")->kind() == JsonValue::Kind::null);
  CHECK(stream.find("stream")->find("form")->as_int64() == form);
  REQUIRE(stream.find("stream")->find("form_path")->as_array().size() == 1);
  CHECK(stream.find("stream")->find("form_path")->as_array().front().as_int64() == form);
}

TEST_CASE("inspect answers the same before and after a text read") {
  Fixture fixture;
  const std::uint32_t form = fixture.form(world(), flate(FlateDamage::corrupt, 4));
  fixture.page(hello() + "/X1 Do\n", {}, "", "/XObject << /X1 " + std::to_string(form) + " 0 R >>");
  fixture.page(hello() + world(), flate(FlateDamage::truncated));
  const std::string file = fixture.bytes();
  // The damaged_pages member, written last, as bytes.
  const auto member = [](const std::string& answer) {
    return answer.substr(answer.find("\"damaged_pages\""));
  };
  std::string first;
  {
    const Opened opened(file);
    pdfkit_buffer out{nullptr, 0, nullptr};
    REQUIRE(pdfkit_inspect(opened.get(), nullptr, &out) == PDFKIT_OK);
    first = take(out);
  }
  const Opened opened(file);
  (void)text_of(opened.get(), R"({"pages": [2]})");
  pdfkit_buffer out{nullptr, 0, nullptr};
  REQUIRE(pdfkit_inspect(opened.get(), nullptr, &out) == PDFKIT_OK);
  const std::string second = take(out);
  CHECK(member(second) == member(first));
  // Inspect read every page, so its warnings name both, in the order met.
  CHECK(parse(second).find("warnings")->as_array().size() == 2);
  CHECK(named(parse(first)) == std::vector<std::string>{"1:flate_corrupt", "2:flate_truncated"});
}

TEST_CASE("a limit met while inspect reads a page names it as not checked") {
  Fixture fixture;
  fixture.page(hello());
  fixture.page(hello() + world() + hello() + world());
  const Opened opened(fixture.bytes(), R"({"operators_per_page": 12})");
  const JsonValue answer = inspect(opened.get());
  REQUIRE(named(answer) == std::vector<std::string>{"2:not_checked"});
  const JsonValue& stream = damaged(answer).front().find("streams")->as_array().front();
  CHECK(stream.find("stream")->kind() == JsonValue::Kind::null);
  CHECK(stream.find("fault")->as_string().find("operators") != std::string::npos);
  CHECK(stream.find("unread_bytes")->kind() == JsonValue::Kind::null);
}

TEST_CASE("text and find name only the pages they read") {
  Fixture fixture;
  fixture.page(hello() + world(), flate(FlateDamage::checksum));
  fixture.page(hello() + world(), flate(FlateDamage::truncated));
  const Opened opened(fixture.bytes());
  CHECK(named(text_of(opened.get(), R"({"pages": [2]})")) ==
        std::vector<std::string>{"2:flate_truncated"});
  CHECK(named(find_of(opened.get(), R"({"values": [{"text": "Hello"}], "pages": [1]})")) ==
        std::vector<std::string>{"1:flate_checksum"});
}

TEST_CASE("the write report says whether a search ran over a damaged page and what it copied") {
  Fixture fixture;
  // Page 1: the search edits it; its truncated stream is written again from
  // what it decoded, so nothing of the lost tail is copied.
  fixture.page(hello() + world(), flate(FlateDamage::truncated));
  // Page 2: the search finds nothing there; the damaged stream is copied as
  // it was.
  fixture.page(world() + world(), flate(FlateDamage::corrupt, world().size()));
  // Page 3: the search edits it, and the bytes no operator draws are kept.
  fixture.page(hello() + "1 2 zork\n" + world());
  const std::string file = fixture.bytes();
  const auto [report, output] = write_of(file, search_hello);
  const JsonValue::Array& pages = damaged(report);
  REQUIRE(pages.size() == 3);
  CHECK(keys_of(pages.front()) == "page source_page glyphs streams text_redaction copied_as_is ");
  for (const JsonValue& page : pages) {
    CHECK(page.find("text_redaction")->as_bool());
  }
  CHECK_FALSE(pages[0].find("copied_as_is")->as_bool());
  CHECK(pages[1].find("copied_as_is")->as_bool());
  CHECK(pages[2].find("copied_as_is")->as_bool());

  // Without a text redaction, every damaged page is copied as it was.
  const auto [plain, copy] = write_of(file, "");
  for (const JsonValue& page : damaged(plain)) {
    CHECK_FALSE(page.find("text_redaction")->as_bool());
    CHECK(page.find("copied_as_is")->as_bool());
  }
}

TEST_CASE("the write report names output pages with the source page each one is") {
  Fixture fixture;
  fixture.page(hello());
  fixture.page(hello() + world(), flate(FlateDamage::checksum));
  const Opened opened(fixture.bytes());
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_edit* edit = nullptr;
  REQUIRE(pdfkit_edit_new(opened.get(), &edit, &out) == PDFKIT_OK);
  (void)take(out);
  REQUIRE(pdfkit_select_pages(edit, R"({"pages": [2, 1]})", &out) == PDFKIT_OK);
  (void)take(out);
  pdfkit_buffer pdf{nullptr, 0, nullptr};
  REQUIRE(pdfkit_write(edit, nullptr, &pdf, &out) == PDFKIT_OK);
  const JsonValue report = parse(take(out));
  (void)take(pdf);
  pdfkit_edit_free(edit);
  REQUIRE(damaged(report).size() == 1);
  CHECK(damaged(report).front().find("page")->as_int64() == 1);
  CHECK(damaged(report).front().find("source_page")->as_int64() == 2);
}
