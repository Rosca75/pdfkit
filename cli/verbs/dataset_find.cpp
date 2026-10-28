// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The find member of a dataset outcome. Agreement is judged value by value
// on the sorted glyph ranges each function names (page, first, last): every
// range of every occurrence pdfkit_find reports against every occurrence
// pdfkit_redact_text answers, which is one range per stretch. The feedback
// redacts with an empty placeholder, so nothing drawn can spell a value
// again, and searches only the page it changed. Each fed-back occurrence
// carries the value's occurrences on its page before and after (their
// rectangles and hidden classes), so the gate counts a place once and
// leaves out what every write removes (tests/dataset/gates/phase12.py).

#include "verbs/dataset_find.hpp"

#include "json_text.hpp"
#include "output.hpp"
#include "support.hpp"
#include "verbs/dataset_redact.hpp"
#include "verbs/dataset_write.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace pdfkit::cli::verbs {

namespace {

// The occurrences fed back per file, at most: the first of each of the
// first values found.
constexpr std::size_t feedback_limit = 2;
// Disagreements named in the outcome, at most.
constexpr std::size_t max_named = 5;

struct Call {
  pdfkit_status status = PDFKIT_E_INTERNAL;
  std::string text;
};

Call take(pdfkit_status status, pdfkit_buffer& out) {
  Call call{status, std::string(reinterpret_cast<const char*>(out.data), out.size)};
  pdfkit_buffer_free(&out);
  return call;
}

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

using Range = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;

std::string ranges_json(const std::vector<Range>& ranges) {
  std::string out = "[";
  for (std::size_t i = 0; i < ranges.size() && i < max_named; ++i) {
    const auto [page, first, last] = ranges[i];
    out += (i == 0 ? "[" : ", [") + std::to_string(page) + ", " + std::to_string(first) + ", " +
           std::to_string(last) + "]";
  }
  return out + "]";
}

std::string values_json(const std::vector<std::string>& words, const std::string& extra) {
  std::string out = R"({"values": [)";
  for (std::size_t i = 0; i < words.size(); ++i) {
    out += (i == 0 ? "" : ", ") + std::string(R"({"text": )") + json_quote(words[i]) + "}";
  }
  return out + "]" + extra + "}";
}

// One occurrence pdfkit_find reported: its value, its page, its ranges as
// JSON and its count's value.
struct Picked {
  std::size_t value = 0;
  std::uint64_t page = 0;
  std::string glyphs;
};

// One occurrence as the feedback lists it: its rectangles and hidden
// classes, as pdfkit_find wrote them.
std::string place_of(const JsonText& json, const JsonText::Node& occurrence) {
  const JsonText::Node* rectangles = json.member(occurrence, "rectangles");
  const JsonText::Node* hidden = json.member(occurrence, "hidden");
  return R"({"rectangles": )" + std::string(rectangles != nullptr ? rectangles->raw : "[]") +
         R"(, "hidden": )" + std::string(hidden != nullptr ? hidden->raw : "[]") + "}";
}

// The occurrences of value on page of an output (count and places), or no
// value when the output does not open or the call fails.
std::optional<std::pair<std::uint64_t, std::string>> count_on(const std::string& pdf,
                                                              const std::string& value,
                                                              std::uint64_t page) {
  pdfkit_buffer out{nullptr, 0, nullptr};
  pdfkit_document* doc = nullptr;
  const Call opened = take(pdfkit_open(reinterpret_cast<const std::uint8_t*>(pdf.data()),
                                       pdf.size(), nullptr, &doc, &out),
                           out);
  if (opened.status != PDFKIT_OK) {
    return std::nullopt;
  }
  const std::string request =
      values_json({value}, R"(, "pages": [)" + std::to_string(page) + R"(], "context": 0)");
  const Call found = take(pdfkit_find(doc, request.c_str(), &out), out);
  pdfkit_close(doc);
  std::size_t offset = 0;
  const std::optional<JsonText> json =
      found.status == PDFKIT_OK ? JsonText::parse(found.text, offset) : std::nullopt;
  if (!json) {
    return std::nullopt;
  }
  const JsonText::Node* values = json->member(json->root(), "values");
  if (values == nullptr || values->items.empty()) {
    return std::nullopt;
  }
  const JsonText::Node& first = json->node(values->items.front());
  std::string places = "[";
  if (const JsonText::Node* list = json->member(first, "occurrences")) {
    for (const std::size_t at : list->items) {
      places += (places.size() == 1 ? "" : ", ") + place_of(*json, json->node(at));
    }
  }
  return std::make_pair(number_of(json->member(first, "count")), places + "]");
}

}  // namespace

std::string dataset_find(pdfkit_document* document, std::string_view id,
                         const std::string& text_member, double text_seconds,
                         const std::string& out_dir) {
  std::size_t offset = 0;
  const std::optional<JsonText> member = JsonText::parse(text_member, offset);
  const JsonText::Node* text = member ? member->member(member->root(), "text") : nullptr;
  std::vector<std::string> words =
      dataset_redaction_words(id, text != nullptr ? std::string_view(text->text) : "");
  words.emplace_back(dataset_redaction_marker);

  pdfkit_buffer out{nullptr, 0, nullptr};
  const std::string request = values_json(words, "");
  const auto started = std::chrono::steady_clock::now();
  const Call found = take(pdfkit_find(document, request.c_str(), &out), out);
  const double find_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  if (found.status != PDFKIT_OK) {
    return "{" + dataset_failure(found.status, found.text) + "}";
  }
  std::string path = out_dir;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  const std::string timing = R"({"text": )" + std::to_string(text_seconds) + R"(, "find": )" +
                             std::to_string(find_seconds) + "}\n";
  (void)write_output_file("dataset", path + "outputs/" + std::string(id) + ".find-time.json",
                          reinterpret_cast<const std::uint8_t*>(timing.data()), timing.size());

  // The ranges find names, per value, and the occurrences to feed back.
  const std::optional<JsonText> answer = JsonText::parse(found.text, offset);
  if (!answer) {
    return "{" +
           dataset_failure(PDFKIT_E_INTERNAL, "pdfkit_find returned JSON that does not read") + "}";
  }
  const JsonText::Node* fingerprint = answer->member(answer->root(), "fingerprint");
  const JsonText::Node* values = answer->member(answer->root(), "values");
  std::vector<std::vector<Range>> find_ranges(words.size());
  std::vector<std::uint64_t> counts(words.size(), 0);
  std::vector<std::vector<std::uint64_t>> pages_of(words.size());
  // Per value, each occurrence's page and place.
  std::vector<std::vector<std::pair<std::uint64_t, std::string>>> places_of(words.size());
  std::vector<Picked> picked;
  for (std::size_t v = 0; values != nullptr && v < values->items.size() && v < words.size(); ++v) {
    const JsonText::Node& value = answer->node(values->items[v]);
    counts[v] = number_of(answer->member(value, "count"));
    const JsonText::Node* list = answer->member(value, "occurrences");
    for (const std::size_t at : list != nullptr ? list->items : std::vector<std::size_t>{}) {
      const JsonText::Node& occurrence = answer->node(at);
      pages_of[v].push_back(number_of(answer->member(occurrence, "page")));
      places_of[v].emplace_back(pages_of[v].back(), place_of(*answer, occurrence));
      const JsonText::Node* glyphs = answer->member(occurrence, "glyphs");
      for (const std::size_t r : glyphs != nullptr ? glyphs->items : std::vector<std::size_t>{}) {
        const JsonText::Node& range = answer->node(r);
        find_ranges[v].emplace_back(number_of(answer->member(range, "page")),
                                    number_of(answer->member(range, "first")),
                                    number_of(answer->member(range, "last")));
      }
      if (glyphs != nullptr && picked.size() < feedback_limit &&
          (picked.empty() || picked.back().value != v)) {
        picked.push_back(
            {v, number_of(answer->member(occurrence, "page")), std::string(glyphs->raw)});
      }
    }
    std::ranges::sort(find_ranges[v]);
  }

  // The same words through the redaction search, on an edit never written.
  std::string matches = R"({"matches": [)";
  for (std::size_t i = 0; i < words.size(); ++i) {
    matches += (i == 0 ? "" : ", ") + std::string(R"({"search": )") + json_quote(words[i]) + "}";
  }
  matches += "]}";
  pdfkit_edit* edit = nullptr;
  Call redacted = take(pdfkit_edit_new(document, &edit, &out), out);
  if (redacted.status == PDFKIT_OK) {
    redacted = take(pdfkit_redact_text(edit, matches.c_str(), &out), out);
  }
  pdfkit_edit_free(edit);
  if (redacted.status != PDFKIT_OK) {
    return "{" + dataset_failure(redacted.status, redacted.text) + "}";
  }
  const std::optional<JsonText> removed = JsonText::parse(redacted.text, offset);
  if (!removed) {
    return "{" +
           dataset_failure(PDFKIT_E_INTERNAL,
                           "pdfkit_redact_text returned JSON that does not read") +
           "}";
  }
  const JsonText::Node* listed = removed->member(removed->root(), "matches");
  std::string disagreements = "[";
  std::size_t disagreeing = 0;
  for (std::size_t v = 0; v < words.size(); ++v) {
    std::vector<Range> redact_ranges;
    if (listed != nullptr && v < listed->items.size()) {
      const JsonText::Node* list = removed->member(removed->node(listed->items[v]), "occurrences");
      for (const std::size_t at : list != nullptr ? list->items : std::vector<std::size_t>{}) {
        const JsonText::Node& occurrence = removed->node(at);
        const JsonText::Node* glyphs = removed->member(occurrence, "glyphs");
        if (glyphs != nullptr && glyphs->items.size() == 2) {
          redact_ranges.emplace_back(number_of(removed->member(occurrence, "page")),
                                     number_of(&removed->node(glyphs->items[0])),
                                     number_of(&removed->node(glyphs->items[1])));
        }
      }
    }
    std::ranges::sort(redact_ranges);
    if (redact_ranges != find_ranges[v]) {
      if (disagreeing < max_named) {
        disagreements += (disagreeing == 0 ? "" : ", ") + std::string(R"({"value": )") +
                         json_quote(words[v]) + R"(, "find": )" + ranges_json(find_ranges[v]) +
                         R"(, "redact": )" + ranges_json(redact_ranges) + "}";
      }
      ++disagreeing;
    }
  }
  disagreements += ']';

  // Each picked occurrence sent back as a located match, alone on a fresh
  // edit, written, and its page searched again.
  std::string feedback_status = picked.empty() ? "none" : "ok";
  std::size_t exact = 0;
  std::string failures = "[";
  std::string items = "[";
  bool first_failure = true;
  const auto fail = [&](const Picked& item, const std::string& why) {
    failures += (first_failure ? "" : ", ") + std::string(R"({"value": )") +
                json_quote(words[item.value]) + R"(, "page": )" + std::to_string(item.page) +
                R"(, "why": )" + json_quote(why) + "}";
    first_failure = false;
  };
  for (const Picked& item : picked) {
    const std::string located =
        R"({"matches": [{"glyphs": )" + item.glyphs + R"(, "fingerprint": )" +
        json_quote(fingerprint != nullptr ? fingerprint->text : "") + R"(, "placeholder": ""}]})";
    const auto before =
        static_cast<std::uint64_t>(std::ranges::count(pages_of[item.value], item.page));
    edit = nullptr;
    Call call = take(pdfkit_edit_new(document, &edit, &out), out);
    if (call.status == PDFKIT_OK) {
      call = take(pdfkit_redact_text(edit, located.c_str(), &out), out);
      if (call.status == PDFKIT_E_INVALID_ARGUMENT && &item == &picked.front() &&
          call.text.find(R"(\"glyphs\" as an array)") != std::string::npos) {
        // The array form of a located match is not in this build.
        pdfkit_edit_free(edit);
        feedback_status = "unsupported";
        failures = "[";
        break;
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
      fail(item, dataset_failure(call.status, call.text));
      continue;
    }
    const auto after = count_on(pdf, words[item.value], item.page);
    std::string before_places = "[";
    for (const auto& [page, place] : places_of[item.value]) {
      if (page == item.page) {
        before_places += (before_places.size() == 1 ? "" : ", ") + place;
      }
    }
    items += (items.size() == 1 ? "" : ", ") + std::string(R"({"value": )") +
             json_quote(words[item.value]) + R"(, "page": )" + std::to_string(item.page) +
             R"(, "glyphs": )" + item.glyphs + R"(, "before": )" + before_places +
             R"(], "after": )" + (after ? after->second : std::string("null")) + "}";
    if (after && after->first + 1 == before) {
      ++exact;
    } else {
      fail(item, "the page held " + std::to_string(before) + " occurrences before and " +
                     (after ? std::to_string(after->first) : std::string("?")) + " after");
    }
  }
  failures += ']';
  items += ']';

  std::string count_list;
  std::uint64_t total = 0;
  for (std::size_t v = 0; v < counts.size(); ++v) {
    count_list += (v == 0 ? "" : ", ") + std::to_string(counts[v]);
    total += counts[v];
  }
  std::string word_list = "[";
  for (std::size_t v = 0; v < words.size(); ++v) {
    word_list += (v == 0 ? "" : ", ") + json_quote(words[v]);
  }
  word_list += ']';
  const bool unsupported = feedback_status == "unsupported";
  return R"({"status": "ok", "code": null, "values": )" + word_list + R"(, "counts": [)" +
         count_list + "], \"occurrences\": " + std::to_string(total) +
         ", \"agree\": " + (disagreeing == 0 ? "true" : "false") +
         ", \"disagreements\": " + disagreements + R"(, "feedback": {"status": )" +
         json_quote(feedback_status) +
         ", \"tried\": " + std::to_string(unsupported ? 0 : picked.size()) +
         ", \"exact\": " + std::to_string(exact) + ", \"failures\": " + failures +
         ", \"items\": " + items + "}}";
}

}  // namespace pdfkit::cli::verbs
