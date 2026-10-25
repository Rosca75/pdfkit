// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The union of two targets' changes, for a form whose glyphs are removed by
// occurrences on several pages: every removed code and every operator's
// records are kept; where both name a placeholder or a rewritten operation at
// one place, the first target's stays, so the merge is the same whatever
// order the pages are merged in only when they agree, which they do, because
// each page computes a place's change from the same form bytes.

#include "redact/text_plan.hpp"

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

}  // namespace pdfkit::redact
