// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The redact_text member of a dataset outcome. The words are the maximal runs
// of letters in the text (an ASCII letter, or a code point from U+00C0 up
// outside the punctuation, symbol, private-use and specials blocks, which is
// what a letter of the dataset's scripts is), of four code points or more,
// each once and sorted by their bytes; SplitMix64 seeded from the SHA-256 of
// "8:<id>" draws five of them without replacement, so the job depends on the
// file's id and pdfkit's own reading of it alone. The located member redacts
// one occurrence of a word by its glyph range: the words of pdfkit_text's
// blocks made of letters alone, three or more, that occur twice or more in
// the document (no part of the placeholder "[REDACTED]", read without case);
// SplitMix64 seeded from "11:<id>" draws one word and one of its visible
// occurrences (no hidden class, not hyphenated), preferring a word drawn at
// one place on two pages or more (a running header or footer, which is
// often a form drawn on every page), then whether the match is sent as a
// glyph range or as an occurrence (its index among pdfkit_find's
// occurrences of the word on the page, half the files each); the output is
// read back and the word's occurrences counted before and after, with each
// copy's page, box and hidden classes, so the gate counts the copies at one
// place once.

#include "verbs/dataset_redact.hpp"

#include "json_text.hpp"
#include "output.hpp"
#include "sha256.hpp"
#include "support.hpp"
#include "verbs/dataset_write.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
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

// The four numbers of a box written as [left, bottom, right, top].
std::array<double, 4> box_of(std::string_view raw) {
  std::array<double, 4> out{};
  std::size_t at = 0;
  for (double& value : out) {
    while (at < raw.size() && (raw[at] == '[' || raw[at] == ',' || raw[at] == ' ')) {
      ++at;
    }
    const auto [end, error] = std::from_chars(raw.data() + at, raw.data() + raw.size(), value);
    if (error != std::errc()) {
      return {};
    }
    at = static_cast<std::size_t>(end - raw.data());
  }
  return out;
}

// The words of a pdfkit_text answer: page, text, glyph range, box, hidden
// classes, and whether the word is visible (in a visible block, no hidden
// class, not hyphenated, a box of some width and height: a word drawn at
// size 0 shows nothing); a duplicate copy is marked, because it is no word
// of its own but is a copy at the word's place.
struct Word {
  std::string page;
  std::string text;
  std::string first;
  std::string last;
  std::string box;
  std::string hidden;
  bool visible = false;
  bool duplicate = false;
};

std::vector<Word> words_of(const JsonText& answer) {
  std::vector<Word> out;
  const JsonText::Node* pages = answer.member(answer.root(), "pages");
  for (const std::size_t at : pages != nullptr ? pages->items : std::vector<std::size_t>{}) {
    const JsonText::Node& page = answer.node(at);
    const JsonText::Node* number = answer.member(page, "page");
    for (const std::string_view key : {"blocks", "hidden_blocks"}) {
      const JsonText::Node* blocks = answer.member(page, key);
      for (const std::size_t b : blocks != nullptr ? blocks->items : std::vector<std::size_t>{}) {
        const JsonText::Node* lines = answer.member(answer.node(b), "lines");
        for (const std::size_t l : lines != nullptr ? lines->items : std::vector<std::size_t>{}) {
          const JsonText::Node* words = answer.member(answer.node(l), "words");
          for (const std::size_t w : words != nullptr ? words->items : std::vector<std::size_t>{}) {
            const JsonText::Node& item = answer.node(w);
            const JsonText::Node* text = answer.member(item, "text");
            const JsonText::Node* glyphs = answer.member(item, "glyphs");
            const JsonText::Node* box = answer.member(item, "box");
            const JsonText::Node* hidden = answer.member(item, "hidden");
            const JsonText::Node* hyphenated = answer.member(item, "hyphenated");
            if (number == nullptr || text == nullptr || glyphs == nullptr ||
                glyphs->items.size() != 2 || box == nullptr) {
              continue;
            }
            // A word whose only hidden class is duplicate is a copy of a word
            // already read, as the page's text reads it, and goes with it.
            Word word;
            word.duplicate = hidden != nullptr && hidden->items.size() == 1 &&
                             answer.node(hidden->items.front()).text == "duplicate";
            word.hidden = hidden != nullptr ? std::string(hidden->raw) : "[]";
            word.page = std::string(number->raw);
            word.text = text->text;
            word.first = std::string(answer.node(glyphs->items[0]).raw);
            word.last = std::string(answer.node(glyphs->items[1]).raw);
            word.box = std::string(box->raw);
            const std::array<double, 4> area = box_of(word.box);
            word.visible = key == "blocks" && hidden != nullptr && hidden->items.empty() &&
                           area[2] > area[0] && area[3] > area[1] && hyphenated != nullptr &&
                           hyphenated->text == "false";
            out.push_back(std::move(word));
          }
        }
      }
    }
  }
  return out;
}

// Whether word is a candidate of the located job: letters alone, three or
// more, and no part of the placeholder read without case.
bool located_word(std::string_view word) {
  std::size_t at = 0;
  std::size_t letters = 0;
  std::string lowered;
  while (at < word.size()) {
    const std::size_t start = at;
    const char32_t value = next(word, at);
    if (!letter(value)) {
      return false;
    }
    ++letters;
    for (std::size_t i = start; i < at; ++i) {
      const char c = word[i];
      lowered += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
  }
  return letters >= 3 && std::string_view("[redacted]").find(lowered) == std::string_view::npos;
}

// The pdfkit_text answer of a document with its blocks.
Call text_of(pdfkit_document* document) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  return take(pdfkit_text(document, R"({"blocks": true})", &out), out);
}

std::size_t occurrences_of(const std::vector<Word>& words, const std::string& text) {
  return static_cast<std::size_t>(std::ranges::count_if(
      words, [&text](const Word& word) { return !word.duplicate && word.text == text; }));
}

// Every copy of text among words, duplicates included: page, box, hidden.
std::string places_of(const std::vector<Word>& words, const std::string& text) {
  std::string out = "[";
  for (const Word& word : words) {
    if (word.text == text) {
      out += (out.size() == 1 ? "" : ", ") + std::string(R"({"page": )") + word.page +
             R"(, "box": )" + word.box + R"(, "hidden": )" + word.hidden + "}";
    }
  }
  return out + "]";
}

// Whether word is drawn at the same place (within a point) on another page,
// among the words of its text (indices).
bool repeated(const std::vector<Word>& words, const std::vector<std::size_t>& indices,
              const Word& word) {
  const std::array<double, 4> box = box_of(word.box);
  return std::ranges::any_of(indices, [&](std::size_t i) {
    const Word& other = words[i];
    if (other.page == word.page) {
      return false;
    }
    const std::array<double, 4> place = box_of(other.box);
    for (std::size_t k = 0; k < box.size(); ++k) {
      if (std::abs(box.at(k) - place.at(k)) > 1.0) {
        return false;
      }
    }
    return true;
  });
}

// The index of the occurrence of text that pdfkit_find reports on page as
// the one range [first, last), or no value.
std::optional<std::size_t> find_index(pdfkit_document* document, const Word& word) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  const std::string request = R"({"values": [{"text": )" + json_quote(word.text) +
                              R"(}], "pages": [)" + word.page + R"(], "context": 0})";
  const Call found = take(pdfkit_find(document, request.c_str(), &out), out);
  std::size_t offset = 0;
  const std::optional<JsonText> json =
      found.status == PDFKIT_OK ? JsonText::parse(found.text, offset) : std::nullopt;
  if (!json) {
    return std::nullopt;
  }
  const JsonText& answer = *json;
  const JsonText::Node* values = answer.member(answer.root(), "values");
  if (values == nullptr || values->items.empty()) {
    return std::nullopt;
  }
  const JsonText::Node* list = answer.member(answer.node(values->items.front()), "occurrences");
  for (std::size_t i = 0; list != nullptr && i < list->items.size(); ++i) {
    const JsonText::Node* glyphs = answer.member(answer.node(list->items[i]), "glyphs");
    if (glyphs == nullptr || glyphs->items.size() != 1) {
      continue;
    }
    const JsonText::Node& range = answer.node(glyphs->items.front());
    const JsonText::Node* first = answer.member(range, "first");
    const JsonText::Node* last = answer.member(range, "last");
    if (first != nullptr && last != nullptr && first->raw == word.first && last->raw == word.last) {
      return i;
    }
  }
  return std::nullopt;
}

// The located member: one occurrence of a repeated word redacted by its
// glyph range.
std::string located(pdfkit_document* document, std::string_view id, const std::string& out_dir) {
  const Call read = text_of(document);
  std::size_t offset = 0;
  const std::optional<JsonText> answer =
      read.status == PDFKIT_OK ? JsonText::parse(read.text, offset) : std::nullopt;
  if (!answer) {
    return "{" + dataset_failure(read.status, read.text) + "}";
  }
  const JsonText::Node* fingerprint = answer->member(answer->root(), "fingerprint");
  const std::vector<Word> words = words_of(*answer);
  std::map<std::string, std::vector<std::size_t>> by_text;
  for (std::size_t i = 0; i < words.size(); ++i) {
    if (!words[i].duplicate && located_word(words[i].text)) {
      by_text[words[i].text].push_back(i);
    }
  }
  // The visible occurrences a word may be picked at: those drawn at one
  // place on several pages when any word has one.
  std::map<std::string, std::vector<std::size_t>> at_repeated;
  for (const auto& [text, indices] : by_text) {
    for (const std::size_t i : indices) {
      if (indices.size() >= 2 && words[i].visible && repeated(words, indices, words[i])) {
        at_repeated[text].push_back(i);
      }
    }
  }
  std::vector<std::string> pool;
  for (const auto& [text, indices] : by_text) {
    if (indices.size() >= 2 &&
        std::ranges::any_of(indices, [&words](std::size_t i) { return words[i].visible; }) &&
        (at_repeated.empty() || at_repeated.contains(text))) {
      pool.push_back(text);
    }
  }
  if (pool.empty() || fingerprint == nullptr) {
    return R"({"status": "skipped", "code": "no_candidate"})";
  }
  const std::string seed_text = "11:" + std::string(id);
  const std::string digest =
      Sha256::hex(reinterpret_cast<const std::uint8_t*>(seed_text.data()), seed_text.size());
  std::uint64_t state = 0;
  (void)std::from_chars(digest.data(), digest.data() + 16, state, 16);
  const std::string chosen = pool[dataset_splitmix(state) % pool.size()];
  std::vector<std::size_t> visible;
  for (const std::size_t i : by_text[chosen]) {
    if (words[i].visible) {
      visible.push_back(i);
    }
  }
  if (const auto shared = at_repeated.find(chosen); shared != at_repeated.end()) {
    visible = shared->second;
  }
  const Word& word = words[visible[dataset_splitmix(state) % visible.size()]];
  const std::size_t before = by_text[chosen].size();
  // Half the files name the occurrence by its index in pdfkit_find's order.
  std::optional<std::size_t> index;
  if (dataset_splitmix(state) % 2 == 0) {
    index = find_index(document, word);
  }
  const std::string matches =
      index ? R"({"matches": [{"occurrence": {"text": )" + json_quote(chosen) + R"(, "page": )" +
                  word.page + R"(, "index": )" + std::to_string(*index) +
                  R"(}, "placeholder": "[REDACTED]"}]})"
            : R"({"matches": [{"glyphs": {"page": )" + word.page + R"(, "first": )" + word.first +
                  R"(, "last": )" + word.last + R"(}, "fingerprint": )" +
                  json_quote(fingerprint->text) + R"(, "placeholder": "[REDACTED]"}]})";
  const std::string head = R"("word": )" + json_quote(chosen) + ", \"page\": " + word.page +
                           ", \"glyphs\": [" + word.first + ", " + word.last +
                           "], \"box\": " + word.box +
                           ", \"form\": " + (index ? "\"occurrence\"" : "\"glyphs\"") +
                           ", \"repeated\": " + (at_repeated.contains(chosen) ? "true" : "false") +
                           ", \"before\": " + std::to_string(before) +
                           ", \"places_before\": " + places_of(words, chosen);

  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_edit* edit = nullptr;
  Call call = take(pdfkit_edit_new(document, &edit, &out), out);
  if (call.status == PDFKIT_OK) {
    call = take(pdfkit_redact_text(edit, matches.c_str(), &out), out);
  }
  // Whether the occurrence resolved to the glyphs of the word it names.
  std::string same = "null";
  if (index && call.status == PDFKIT_OK) {
    same = "false";
    std::size_t at = 0;
    if (const std::optional<JsonText> answer_json = JsonText::parse(call.text, at)) {
      const JsonText::Node* list = answer_json->member(answer_json->root(), "matches");
      const JsonText::Node* first_match = list != nullptr && !list->items.empty()
                                              ? &answer_json->node(list->items.front())
                                              : nullptr;
      const JsonText::Node* found =
          first_match != nullptr ? answer_json->member(*first_match, "occurrences") : nullptr;
      const JsonText::Node* glyphs =
          found != nullptr && found->items.size() == 1
              ? answer_json->member(answer_json->node(found->items.front()), "glyphs")
              : nullptr;
      if (glyphs != nullptr && glyphs->raw == "[[" + word.first + "," + word.last + "]]") {
        same = "true";
      }
    }
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
    return "{" + dataset_failure(call.status, call.text) + ", " + head +
           ", \"same_glyphs\": " + same + "}";
  }
  std::string self_check = "null";
  if (const std::optional<JsonText> report = JsonText::parse(call.text, offset)) {
    if (const JsonText::Node* check = report->member(report->root(), "self_check")) {
      if (const JsonText::Node* status = report->member(*check, "status")) {
        self_check = json_quote(status->text);
      }
    }
  }
  // The word's occurrences in the output, read back the same way.
  std::string after = "null";
  std::string places_after = "null";
  {
    pdfkit_document* written = nullptr;
    const Call opened = take(pdfkit_open(reinterpret_cast<const std::uint8_t*>(pdf.data()),
                                         pdf.size(), nullptr, &written, &out),
                             out);
    if (opened.status == PDFKIT_OK) {
      const Call again = text_of(written);
      if (const std::optional<JsonText> reread =
              again.status == PDFKIT_OK ? JsonText::parse(again.text, offset) : std::nullopt) {
        const std::vector<Word> written_words = words_of(*reread);
        after = std::to_string(occurrences_of(written_words, chosen));
        places_after = places_of(written_words, chosen);
      }
    }
    pdfkit_close(written);
  }
  std::string path = out_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  const std::string relative = "outputs/" + std::string(id) + ".located.pdf";
  const auto* data = reinterpret_cast<const std::uint8_t*>(pdf.data());
  if (!write_output_file("dataset", path + relative, data, pdf.size())) {
    return R"({"status": "error", "code": "output_failed", "message": "the output could not be written"})";
  }
  return R"({"status": "ok", "code": null, )" + head + ", \"after\": " + after +
         ", \"places_after\": " + places_after + ", \"same_glyphs\": " + same +
         ", \"self_check\": " + self_check + ", \"output\": " + json_quote(relative) +
         ", \"sha256\": " + json_quote(Sha256::hex(data, pdf.size())) + "}";
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
                           const std::string& text_member, const DatasetMetadata& metadata,
                           const std::string& out_dir) {
  // The located job first: it reads the document alone.
  const std::string located_member = located(document, id, out_dir);
  std::size_t offset = 0;
  const std::optional<JsonText> member = JsonText::parse(text_member, offset);
  const JsonText::Node* text = member ? member->member(member->root(), "text") : nullptr;
  std::vector<std::string> strings =
      dataset_redaction_words(id, text != nullptr ? std::string_view(text->text) : "");
  strings.emplace_back(dataset_redaction_marker);
  // The fields holding a word are rewritten, and their original values
  // searched in the page text too, after the words.
  std::vector<std::string> rewritten;
  std::vector<std::string> originals;
  const std::string fields = dataset_metadata_rewrite(metadata, strings, rewritten, originals);
  for (const std::string& original : originals) {
    if (std::ranges::find(strings, original) == strings.end()) {
      strings.push_back(original);
    }
  }
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
  if (call.status == PDFKIT_OK && !fields.empty()) {
    call = take(pdfkit_set_metadata(edit, fields.c_str(), &out), out);
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
    return "{" + dataset_failure(call.status, call.text) + ", \"strings\": " + list(strings) +
           ", \"metadata\": " + list(rewritten) + ", \"located\": " + located_member + "}";
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
         ", \"metadata\": " + list(rewritten) + ", \"reopen\": " + dataset_reopen(pdf) +
         ", \"located\": " + located_member + "}";
}

}  // namespace pdfkit::cli::verbs
