// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The union of two targets' changes, for a form whose glyphs are removed by
// occurrences on several pages: every removed code and every operator's
// records are kept; where both name a placeholder or a rewritten operation at
// one place, the first target's stays, so the merge is the same whatever
// order the pages are merged in only when they agree, which they do, because
// each page computes a place's change from the same form bytes. A located
// match's ranges are checked before anything is recorded, so a refused call
// leaves the plan as it was. An occurrence match is resolved through the
// search pdfkit_find runs (MatchFinder::locate) in its order, so the index
// a caller read from pdfkit_find names the same occurrence here; the
// narrowings apply together and must leave exactly one, because pdfkit
// applies no rule of its own to choose (CLAUDE.md section 5).

#include "redact/text_plan.hpp"

#include "platform/error.hpp"
#include "platform/unicode/folding.hpp"
#include "text/page_text.hpp"
#include "text/text_reader.hpp"

#include <algorithm>
#include <string_view>

namespace pdfkit::redact {

void TextPlan::Target::merge(const Target& other) {
  for (const auto& [index, records] : other.operations) {
    operations.try_emplace(index, records);
  }
  removed.insert(other.removed.begin(), other.removed.end());
  for (const auto& [key, insertion] : other.insertions) {
    insertions.try_emplace(key, insertion);
  }
  for (const auto& [index, bytes] : other.replaced) {
    replaced.try_emplace(index, bytes);
  }
  comments = comments || other.comments;
}

namespace {

// text folded under the match's folding, without white space at its ends.
std::u32string folded(std::string_view text, const MatchFinder::Folding& folding) {
  using platform::unicode::Folding;
  std::u32string out = Folding::fold(Folding::decode(text), folding.text).text;
  while (!out.empty() && Folding::is_white_space(out.back())) {
    out.pop_back();
  }
  std::size_t lead = 0;
  while (lead < out.size() && Folding::is_white_space(out[lead])) {
    ++lead;
  }
  return out.substr(lead);
}

// Whether a rectangle meets near (an edge in common counts).
bool meets(const text::Rect& box, const text::Rect& near) {
  return !box.is_empty() && box.left <= near.right && near.left <= box.right &&
         box.bottom <= near.top && near.bottom <= box.top;
}

}  // namespace

std::vector<TextPlan::Range> TextPlan::resolve(syntax::Document& document,
                                               const OccurrenceQuery& query,
                                               const std::string& where) {
  const std::size_t page_count = document.pages().size();
  const std::string expected =
      "an occurrence {\"text\", \"page\", \"folding\", \"index\", \"near\", \"before\", "
      "\"after\"} whose narrowings leave exactly one occurrence of the text on the page, in "
      "pdfkit_find's order";
  if (query.page < 1 || query.page > page_count) {
    throw platform::Error(
        platform::ErrorCode::invalid_argument,
        "pdfkit_redact_text " + where + " names page " + std::to_string(query.page),
        "a page from 1 to " + std::to_string(page_count),
        "take the page number from this document's pdfkit_find answer");
  }
  text::TextReader reader(document);
  const text::PageText page = reader.read(query.page);
  MatchFinder finder(page);
  MatchFinder::Match match;
  match.search = query.text;
  match.folding = query.folding;
  std::vector<MatchFinder::Located> found = finder.locate(match);
  // pdfkit_find's order: by first glyph.
  std::ranges::stable_sort(found, {}, [](const MatchFinder::Located& item) {
    return item.ranges.empty() ? std::size_t{0} : item.ranges.front().first;
  });
  const std::u32string before = query.before ? folded(*query.before, query.folding) : U"";
  const std::u32string after = query.after ? folded(*query.after, query.folding) : U"";
  // The most words of context pdfkit_find gives on each side.
  constexpr std::size_t context_words = 50;
  std::vector<std::size_t> left;
  for (std::size_t i = 0; i < found.size(); ++i) {
    if (query.index && *query.index != i) {
      continue;
    }
    if (query.near &&
        std::ranges::none_of(finder.rectangles(found[i]),
                             [&query](const text::Rect& box) { return meets(box, *query.near); })) {
      continue;
    }
    if (query.before || query.after) {
      const auto [preceding, following] = finder.context(found[i], context_words);
      if (query.before && !folded(preceding, query.folding).ends_with(before)) {
        continue;
      }
      if (query.after && !folded(following, query.folding).starts_with(after)) {
        continue;
      }
    }
    left.push_back(i);
  }
  const std::string named = "pdfkit_redact_text " + where + " names an occurrence of \"" +
                            query.text + "\" on page " + std::to_string(query.page);
  if (left.empty()) {
    throw platform::Error(
        platform::ErrorCode::occurrence_not_found,
        named + ", and its narrowings leave none of the page's " + std::to_string(found.size()) +
            " occurrence" + (found.size() == 1 ? "" : "s"),
        expected,
        "call pdfkit_find for the text on this page and take the index, rectangle or "
        "context of the occurrence from its answer");
  }
  if (left.size() > 1) {
    std::string indices;
    for (const std::size_t i : left) {
      indices += (indices.empty() ? "" : ", ") + std::to_string(i);
    }
    throw platform::Error(platform::ErrorCode::occurrence_ambiguous,
                          named + ", and its narrowings leave " + std::to_string(left.size()) +
                              " occurrences, with the indices " + indices,
                          expected,
                          "add a narrowing (index, near, before or after) that tells these "
                          "occurrences apart");
  }
  std::vector<Range> out;
  for (const auto& [first, last] : found[left.front()].ranges) {
    out.push_back({query.page, first, last});
  }
  return out;
}

std::vector<TextPlan::Range> TextPlan::located_ranges(std::vector<Range> ranges,
                                                      const std::string& where) {
  const std::string expected =
      "one glyph range {\"page\", \"first\", \"last\"}, or an array of such ranges on one "
      "page, each non-empty and none overlapping another";
  const auto refuse = [&](const std::string& failed, const std::string& fix) {
    throw platform::Error(platform::ErrorCode::invalid_argument,
                          "pdfkit_redact_text " + where + " " + failed, expected, fix);
  };
  if (ranges.empty()) {
    refuse("gives an empty array of glyph ranges", "give at least one range");
  }
  for (const Range& range : ranges) {
    if (range.page != ranges.front().page) {
      refuse("gives glyph ranges on pages " + std::to_string(ranges.front().page) + " and " +
                 std::to_string(range.page),
             "give one expression's ranges on one page, and each other page's as a match of "
             "its own");
    }
    if (range.first >= range.last) {
      refuse("gives the empty glyph range [" + std::to_string(range.first) + ", " +
                 std::to_string(range.last) + ")",
             "give each range with first below last");
    }
  }
  std::ranges::stable_sort(ranges, {}, &Range::first);
  for (std::size_t i = 1; i < ranges.size(); ++i) {
    if (ranges[i].first < ranges[i - 1].last) {
      refuse("gives the overlapping glyph ranges [" + std::to_string(ranges[i - 1].first) + ", " +
                 std::to_string(ranges[i - 1].last) + ") and [" + std::to_string(ranges[i].first) +
                 ", " + std::to_string(ranges[i].last) + ")",
             "give each glyph once: join overlapping ranges into one");
    }
  }
  return ranges;
}

}  // namespace pdfkit::redact
