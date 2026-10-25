// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// pdfkit_font_census: the census executable of the fonts layer's gate. It
// opens one PDF with the library's own reader, runs fonts::FontCensus over it
// and prints one JSON object on standard output: the status, every font with
// its kind, program, code count, unreadable count, rung counts and faults,
// and the decoded text per page. tests/dataset/font_census.py runs it over the
// manifest and compares the text with PDFium's. It is a test tool built
// beside the unit tier, not part of the C ABI, because the census is a
// measurement of an internal layer that no caller of the library asks for.

#include "fonts/font_census.hpp"

#include "fonts/decoding_rung.hpp"
#include "platform/error.hpp"
#include "platform/json_writer.hpp"
#include "syntax/document.hpp"
#include "syntax/limits.hpp"

#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using pdfkit::fonts::DecodingRung;
using pdfkit::fonts::FontCensus;
using pdfkit::platform::JsonWriter;

constexpr DecodingRung rungs[] = {DecodingRung::to_unicode, DecodingRung::glyph_name,
                                  DecodingRung::font_program, DecodingRung::actual_text,
                                  DecodingRung::unreadable};

void write_rungs(JsonWriter& json, const std::array<std::uint64_t, 5>& counts) {
  json.key("rungs").begin_object();
  for (const DecodingRung rung : rungs) {
    json.key(pdfkit::fonts::decoding_rung_name(rung))
        .unsigned_integer(counts[static_cast<std::size_t>(rung)]);
  }
  json.end_object();
}

std::string census(const std::vector<std::byte>& bytes) {
  JsonWriter json;
  json.begin_object();
  try {
    const std::unique_ptr<pdfkit::syntax::Document> document =
        pdfkit::syntax::Document::open(bytes, pdfkit::syntax::Limits{});
    const FontCensus::Report report = FontCensus::run(*document);
    json.key("status").string("ok");
    json.key("codes").unsigned_integer(report.codes);
    json.key("unreadable").unsigned_integer(report.unreadable);
    write_rungs(json, report.rungs);
    json.key("missing_fonts").unsigned_integer(report.missing_fonts);
    json.key("fonts").begin_array();
    for (const FontCensus::Font& font : report.fonts) {
      json.begin_object();
      json.key("object").unsigned_integer(font.object_number);
      json.key("name").string(font.name);
      json.key("kind").string(font.kind);
      json.key("program").string(font.program);
      json.key("encoding").string(font.encoding);
      json.key("to_unicode").boolean(font.to_unicode);
      json.key("codes").unsigned_integer(font.codes);
      json.key("unreadable").unsigned_integer(font.unreadable);
      write_rungs(json, font.rungs);
      json.key("faults").begin_array();
      for (const std::string& fault : font.faults) {
        json.string(fault);
      }
      json.end_array();
      json.end_object();
    }
    json.end_array();
    json.key("pages").begin_array();
    for (const std::string& page : report.pages) {
      json.string(page);
    }
    json.end_array();
  } catch (const pdfkit::platform::Error& error) {
    json.key("status").string(pdfkit::platform::is_refusal(error.code()) ? "refused" : "error");
    json.key("code").string(pdfkit::platform::error_code_name(error.code()));
    json.key("message").string(error.failed());
  } catch (const std::exception& error) {
    json.key("status").string("error");
    json.key("code").string("exception");
    json.key("message").string(error.what());
  }
  json.end_object();
  return json.take();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: pdfkit_font_census <file.pdf>\n";
    return 1;
  }
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::cerr << "pdfkit_font_census: cannot read " << argv[1] << "\n";
    return 1;
  }
  const std::vector<char> raw((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    bytes[i] = static_cast<std::byte>(raw[i]);
  }
  std::cout << census(bytes) << "\n";
  return 0;
}
