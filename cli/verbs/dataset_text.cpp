// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The text member of a dataset outcome, read from pdfkit_text's JSON without
// its blocks: a page's text is pdfkit_text's own (its visible blocks, then its
// hidden blocks, a duplicate copy read once), so the outcome and the function
// read the same words.

#include "verbs/dataset_text.hpp"

#include "json_text.hpp"
#include "output.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace pdfkit::cli::verbs {

namespace {

constexpr std::array<std::string_view, 5> rung_names{"to_unicode", "glyph_name", "font_program",
                                                     "actual_text", "unreadable"};
constexpr std::array<std::string_view, 8> hidden_names{
    "render_invisible", "white_fill",           "clipped",   "off_media",
    "off_crop",         "optional_content_off", "duplicate", "covered"};
// Mismatches named in the outcome, at most.
constexpr std::size_t max_named = 10;

std::uint64_t number_of(const JsonText::Node* node) {
  if (node == nullptr || node->kind != JsonText::Kind::number) {
    return 0;
  }
  std::uint64_t value = 0;
  for (const char c : node->raw) {
    if (c < '0' || c > '9') {
      break;
    }
    value = (value * 10) + static_cast<std::uint64_t>(c - '0');
  }
  return value;
}

std::string failure(pdfkit_status status, const std::string& text) {
  std::size_t offset = 0;
  const std::optional<JsonText> error = JsonText::parse(text, offset);
  const JsonText::Node* code = error ? error->member(error->root(), "code") : nullptr;
  const JsonText::Node* failed = error ? error->member(error->root(), "failed") : nullptr;
  if (status == PDFKIT_E_NOT_IMPLEMENTED) {
    return R"({"status": "not_implemented", "code": "PDFKIT_E_NOT_IMPLEMENTED"})";
  }
  return R"({"status": "error", "code": )" +
         json_quote(code != nullptr ? code->text : std::string("internal")) + R"(, "message": )" +
         json_quote(failed != nullptr ? failed->text : text) + "}";
}

}  // namespace

std::string dataset_text(pdfkit_document* document) {
  pdfkit_buffer buffer{nullptr, 0, nullptr};
  const pdfkit_status status =
      pdfkit_text(document, R"({"blocks": false, "reserialisation": true})", &buffer);
  const std::string result(reinterpret_cast<const char*>(buffer.data), buffer.size);
  pdfkit_buffer_free(&buffer);
  if (status != PDFKIT_OK) {
    return failure(status, result);
  }
  std::size_t offset = 0;
  const std::optional<JsonText> json = JsonText::parse(result, offset);
  const JsonText::Node* pages = json ? json->member(json->root(), "pages") : nullptr;
  if (pages == nullptr) {
    return failure(PDFKIT_E_INTERNAL, "pdfkit_text returned JSON that does not read");
  }
  std::array<std::uint64_t, rung_names.size()> rungs{};
  std::array<std::uint64_t, hidden_names.size()> hidden{};
  std::set<std::string> warnings;
  std::uint64_t streams = 0;
  std::uint64_t identical = 0;
  std::uint64_t damaged = 0;
  std::uint64_t failed = 0;
  std::uint64_t structured = 0;
  std::vector<std::string> mismatches;
  std::string page_list;
  std::string all_text;
  bool first = true;
  for (const std::size_t p : pages->items) {
    const JsonText::Node& page = json->node(p);
    const JsonText::Node* page_text = json->member(page, "text");
    const std::string text = page_text != nullptr ? page_text->text : std::string();
    const JsonText::Node* unreadable = json->member(page, "unreadable");
    const JsonText::Node* hidden_node = json->member(page, "hidden");
    const std::uint64_t unreadable_count =
        number_of(unreadable != nullptr ? json->member(*unreadable, "count") : nullptr);
    const std::uint64_t hidden_count =
        number_of(hidden_node != nullptr ? json->member(*hidden_node, "count") : nullptr);
    if (const JsonText::Node* counts = json->member(page, "rungs")) {
      for (std::size_t r = 0; r < rung_names.size(); ++r) {
        rungs[r] += number_of(json->member(*counts, rung_names[r]));
      }
    }
    if (hidden_node != nullptr) {
      if (const JsonText::Node* classes = json->member(*hidden_node, "classes")) {
        for (std::size_t c = 0; c < hidden_names.size(); ++c) {
          hidden[c] += number_of(json->member(*classes, hidden_names[c]));
        }
      }
    }
    if (const JsonText::Node* list = json->member(page, "warnings")) {
      for (const std::size_t w : list->items) {
        if (const JsonText::Node* code = json->member(json->node(w), "code")) {
          warnings.insert(code->text);
        }
      }
    }
    if (const JsonText::Node* check = json->member(page, "reserialisation")) {
      streams += number_of(json->member(*check, "streams"));
      identical += number_of(json->member(*check, "identical"));
      damaged += number_of(json->member(*check, "damaged"));
      failed += number_of(json->member(*check, "failed"));
      if (const JsonText::Node* named = json->member(*check, "mismatches")) {
        for (const std::size_t m : named->items) {
          if (mismatches.size() < max_named) {
            mismatches.push_back("page " + std::string(json->member(page, "page")->raw) + " " +
                                 json->node(m).text);
          }
        }
      }
    }
    if (const JsonText::Node* ordered = json->member(page, "structure_order");
        ordered != nullptr && ordered->text == "true") {
      ++structured;
    }
    const JsonText::Node* number = json->member(page, "page");
    page_list += std::string(first ? "" : ", ") + R"({"page": )" +
                 std::string(number != nullptr ? number->raw : std::string_view("null")) +
                 R"(, "text": )" + json_quote(text) + R"(, "unreadable": )" +
                 std::to_string(unreadable_count) + R"(, "hidden": )" +
                 std::to_string(hidden_count) + "}";
    all_text += (first ? "" : "\f") + text;
    first = false;
  }
  std::string out = R"({"status": "ok", "code": null, "pages": [)" + page_list + "], ";
  out += R"("text": )" + json_quote(all_text) + R"(, "rungs": {)";
  for (std::size_t r = 0; r < rung_names.size(); ++r) {
    out += (r == 0 ? "" : ", ") + json_quote(rung_names[r]) + ": " + std::to_string(rungs[r]);
  }
  out += R"(}, "hidden_classes": {)";
  for (std::size_t c = 0; c < hidden_names.size(); ++c) {
    out += (c == 0 ? "" : ", ") + json_quote(hidden_names[c]) + ": " + std::to_string(hidden[c]);
  }
  out += R"(}, "warnings": [)";
  bool first_warning = true;
  for (const std::string& code : warnings) {
    out += (first_warning ? "" : ", ") + json_quote(code);
    first_warning = false;
  }
  out += R"(], "structure_order_pages": )" + std::to_string(structured);
  out += R"(, "reserialisation": {"streams": )" + std::to_string(streams) + R"(, "identical": )" +
         std::to_string(identical) + R"(, "damaged": )" + std::to_string(damaged) +
         R"(, "failed": )" + std::to_string(failed) + R"(, "mismatches": [)";
  for (std::size_t m = 0; m < mismatches.size(); ++m) {
    out += (m == 0 ? "" : ", ") + json_quote(mismatches[m]);
  }
  out += "]}}";
  return out;
}

}  // namespace pdfkit::cli::verbs
