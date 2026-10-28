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
// leaves the plan as it was.

#include "redact/text_plan.hpp"

#include "platform/error.hpp"

#include <algorithm>

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
