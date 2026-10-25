// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The redact_text member of a dataset outcome. The words are the maximal runs
// of letters in the text (an ASCII letter, or a code point from U+00C0 up
// outside the punctuation, symbol, private-use and specials blocks, which is
// what a letter of the dataset's scripts is), of four code points or more,
// each once and sorted by their bytes; SplitMix64 seeded from the SHA-256 of
// "8:<id>" draws five of them without replacement, so the job depends on the
// file's id and pdfkit's own reading of it alone.

#include "verbs/dataset_redact.hpp"

#include "json_text.hpp"
#include "output.hpp"
#include "sha256.hpp"
#include "support.hpp"
#include "verbs/dataset_write.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <set>
#include <utility>

namespace pdfkit::cli::verbs {

namespace {

constexpr std::size_t drawn_words = 5;
constexpr std::size_t shortest_word = 4;
constexpr std::size_t shortest_code = 8;  // hexadecimal digits: four bytes

struct Call {
  pdfkit_status status = PDFKIT_E_INTERNAL;
  std::string text;
};

Call take(pdfkit_status status, pdfkit_buffer& out) {
  Call call{status, std::string(reinterpret_cast<const char*>(out.data), out.size)};
  pdfkit_buffer_free(&out);
  return call;
}

[[nodiscard]] bool letter(char32_t value) noexcept {
  if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')) {
    return true;
  }
  if (value < 0xC0 || value == 0xD7 || value == 0xF7) {
    return false;
  }
  const std::pair<char32_t, char32_t> excluded[] = {
      {0x2000, 0x2BFF}, {0x3000, 0x303F}, {0xD800, 0xF8FF}, {0xFE30, 0xFE4F},
      {0xFF00, 0xFF20}, {0xFF3B, 0xFF40}, {0xFF5B, 0xFF65}, {0xFFF0, 0xFFFF}};
  return std::ranges::none_of(excluded, [value](const auto& range) {
    return value >= range.first && value <= range.second;
  });
}

// The next code point of text at at, advancing at; U+FFFD for a bad byte.
[[nodiscard]] char32_t next(std::string_view text, std::size_t& at) noexcept {
  const auto byte = static_cast<unsigned char>(text[at]);
  std::size_t length = byte < 0x80 ? 1 : byte >= 0xF0 ? 4 : byte >= 0xE0 ? 3 : byte >= 0xC0 ? 2 : 0;
  if (length == 0 || at + length > text.size()) {
    ++at;
    return 0xFFFD;
  }
  char32_t value = length == 1 ? byte : byte & (0x7FU >> length);
  for (std::size_t i = 1; i < length; ++i) {
    value = (value << 6U) | (static_cast<unsigned char>(text[at + i]) & 0x3FU);
  }
  at += length;
  return value;
}

std::string list(const std::vector<std::string>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    out += (i == 0 ? "" : ", ") + json_quote(values[i]);
  }
  return out + "]";
}

}  // namespace

std::vector<std::string> dataset_redaction_words(std::string_view id, std::string_view text) {
  std::set<std::string> unique;
  std::size_t at = 0;
  std::string word;
  std::size_t letters = 0;
  const auto close = [&]() {
    if (letters >= shortest_word) {
      unique.insert(word);
    }
    word.clear();
    letters = 0;
  };
  while (at < text.size()) {
    const std::size_t start = at;
    const char32_t value = next(text, at);
    if (letter(value)) {
      word.append(text.substr(start, at - start));
      ++letters;
    } else {
      close();
    }
  }
  close();
  std::vector<std::string> pool(unique.begin(), unique.end());
  const std::string seed_text = "8:" + std::string(id);
  const std::string digest =
      Sha256::hex(reinterpret_cast<const std::uint8_t*>(seed_text.data()), seed_text.size());
  std::uint64_t state = 0;
  (void)std::from_chars(digest.data(), digest.data() + 16, state, 16);
  std::vector<std::string> chosen;
  while (chosen.size() < drawn_words && !pool.empty()) {
    const std::size_t index = dataset_splitmix(state) % pool.size();
    chosen.push_back(pool[index]);
    pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(index));
  }
  return chosen;
}

std::string dataset_redact(pdfkit_document* document, std::string_view id,
                           const std::string& text_member, const std::string& out_dir) {
  std::size_t offset = 0;
  const std::optional<JsonText> member = JsonText::parse(text_member, offset);
  const JsonText::Node* text = member ? member->member(member->root(), "text") : nullptr;
  std::vector<std::string> strings =
      dataset_redaction_words(id, text != nullptr ? std::string_view(text->text) : "");
  strings.emplace_back(dataset_redaction_marker);
  std::string matches = R"({"matches": [)";
  for (std::size_t i = 0; i < strings.size(); ++i) {
    matches += (i == 0 ? "" : ", ") + std::string(R"({"search": )") + json_quote(strings[i]) +
               R"(, "placeholder": "[REDACTED]"})";
  }
  matches += "]}";

  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_edit* edit = nullptr;
  Call call = take(pdfkit_edit_new(document, &edit, &out), out);
  std::string answer;
  if (call.status == PDFKIT_OK) {
    call = take(pdfkit_redact_text(edit, matches.c_str(), &out), out);
    answer = call.text;
  }
  std::string pdf;
  if (call.status == PDFKIT_OK) {
    pdfkit_buffer bytes{nullptr, 0, nullptr};
    call = take(pdfkit_write(edit, nullptr, &bytes, &out), out);
    pdf.assign(reinterpret_cast<const char*>(bytes.data), bytes.size);
    pdfkit_buffer_free(&bytes);
  }
  pdfkit_edit_free(edit);
  if (call.status != PDFKIT_OK) {
    return "{" + dataset_failure(call.status, call.text) + ", \"strings\": " + list(strings) + "}";
  }

  // The counts and the removed codes, from the answer of pdfkit_redact_text.
  std::vector<std::string> counts;
  std::set<std::string> codes;
  std::size_t occurrences = 0;
  if (const std::optional<JsonText> found = JsonText::parse(answer, offset)) {
    if (const JsonText::Node* items = found->member(found->root(), "matches")) {
      for (const std::size_t index : items->items) {
        const JsonText::Node& item = found->node(index);
        const JsonText::Node* count = found->member(item, "count");
        counts.emplace_back(count != nullptr ? std::string(count->raw) : "0");
        const JsonText::Node* list_node = found->member(item, "occurrences");
        for (const std::size_t at :
             list_node != nullptr ? list_node->items : std::vector<std::size_t>{}) {
          ++occurrences;
          if (const JsonText::Node* runs = found->member(found->node(at), "codes")) {
            for (const std::size_t run : runs->items) {
              if (found->node(run).text.size() >= shortest_code) {
                codes.insert(found->node(run).text);
              }
            }
          }
        }
      }
    }
  }
  // The rectangles and placeholders, from the write report.
  std::string rectangles = "[";
  std::string placeholders = "[";
  std::string pages = "null";
  std::string self_check = "null";
  if (const std::optional<JsonText> report = JsonText::parse(call.text, offset)) {
    if (const JsonText::Node* count = report->member(report->root(), "page_count")) {
      pages = std::string(count->raw);
    }
    if (const JsonText::Node* check = report->member(report->root(), "self_check")) {
      if (const JsonText::Node* status = report->member(*check, "status")) {
        self_check = json_quote(status->text);
      }
    }
    if (const JsonText::Node* changed = report->member(report->root(), "rectangles")) {
      bool first_box = true;
      bool first_page = true;
      for (const std::size_t index : changed->items) {
        const JsonText::Node& entry = report->node(index);
        const JsonText::Node* page = report->member(entry, "page");
        const JsonText::Node* drawn = report->member(entry, "placeholders");
        const std::string number = page != nullptr ? std::string(page->raw) : "0";
        placeholders += (first_page ? "" : ", ") + std::string("[") + number + ", " +
                        (drawn != nullptr ? std::string(drawn->raw) : "0") + "]";
        first_page = false;
        if (const JsonText::Node* boxes = report->member(entry, "boxes")) {
          for (const std::size_t box : boxes->items) {
            const std::string_view raw = report->node(box).raw;
            rectangles += (first_box ? "" : ", ") + std::string("[") + number + ", " +
                          std::string(raw.substr(1));
            first_box = false;
          }
        }
      }
    }
  }
  rectangles += ']';
  placeholders += ']';
  // The crop box and the rotation of each page the rectangles name (the job
  // selects no page, so an output page is its source page): a renderer draws
  // the crop box turned by the rotation, so a comparison of renderings places
  // the rectangles through them.
  std::string geometry = "[";
  {
    std::set<std::string> numbered;
    if (const std::optional<JsonText> report = JsonText::parse(call.text, offset)) {
      if (const JsonText::Node* changed = report->member(report->root(), "rectangles")) {
        for (const std::size_t index : changed->items) {
          if (const JsonText::Node* page = report->member(report->node(index), "page")) {
            numbered.insert(std::string(page->raw));
          }
        }
      }
    }
    pdfkit_buffer inspected{nullptr, 0, nullptr};
    const Call inspect = take(pdfkit_inspect(document, nullptr, &inspected), inspected);
    const std::optional<JsonText> shape = JsonText::parse(inspect.text, offset);
    const JsonText* pages_of = shape && inspect.status == PDFKIT_OK ? &*shape : nullptr;
    const JsonText::Node* list =
        pages_of != nullptr ? pages_of->member(pages_of->root(), "pages") : nullptr;
    bool first = true;
    for (const std::size_t index : list != nullptr ? list->items : std::vector<std::size_t>{}) {
      const JsonText::Node& entry = pages_of->node(index);
      const JsonText::Node* page = pages_of->member(entry, "page");
      const JsonText::Node* crop = pages_of->member(entry, "crop_box");
      const JsonText::Node* rotate = pages_of->member(entry, "rotate");
      if (page == nullptr || crop == nullptr || rotate == nullptr ||
          !numbered.contains(std::string(page->raw))) {
        continue;
      }
      geometry += (first ? "[" : ", [") + std::string(page->raw) + ", " +
                  std::string(crop->raw.substr(1, crop->raw.size() - 2)) + ", " +
                  std::string(rotate->raw) + "]";
      first = false;
    }
  }
  geometry += ']';

  std::string path = out_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  const std::string relative = "outputs/" + std::string(id) + ".redact.pdf";
  const auto* data = reinterpret_cast<const std::uint8_t*>(pdf.data());
  if (!write_output_file("dataset", path + relative, data, pdf.size())) {
    return R"({"status": "error", "code": "output_failed", "message": "the output could not be written"})";
  }
  return R"({"status": "ok", "code": null, "strings": )" + list(strings) + ", \"counts\": [" +
         [&counts] {
           std::string joined;
           for (std::size_t i = 0; i < counts.size(); ++i) {
             joined += (i == 0 ? "" : ", ") + counts[i];
           }
           return joined;
         }() +
         "], \"occurrences\": " + std::to_string(occurrences) +
         ", \"output\": " + json_quote(relative) +
         ", \"sha256\": " + json_quote(Sha256::hex(data, pdf.size())) +
         ", \"bytes\": " + std::to_string(pdf.size()) + ", \"pages\": " + pages +
         ", \"self_check\": " + self_check + ", \"rectangles\": " + rectangles +
         ", \"placeholders\": " + placeholders + ", \"geometry\": " + geometry +
         ", \"codes\": " + list(std::vector<std::string>(codes.begin(), codes.end())) +
         ", \"reopen\": " + dataset_reopen(pdf) + "}";
}

}  // namespace pdfkit::cli::verbs
