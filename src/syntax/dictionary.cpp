// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Lookup and update of an insertion-ordered dictionary. Lookup is a linear
// walk, because a PDF dictionary holds a handful of keys and a walk keeps the
// order the writer needs without a second index.

#include "syntax/dictionary.hpp"

#include <algorithm>
#include <utility>

namespace pdfkit::syntax {

const Object* Dictionary::find(std::string_view key) const noexcept {
  for (const Entry& entry : entries_) {
    if (entry.first == key) {
      return &entry.second;
    }
  }
  return nullptr;
}

const Object& Dictionary::get(std::string_view key) const noexcept {
  static const Object null_object;
  const Object* value = find(key);
  return value != nullptr ? *value : null_object;
}

void Dictionary::set(std::string key, Object value) {
  for (Entry& entry : entries_) {
    if (entry.first == key) {
      entry.second = std::move(value);
      return;
    }
  }
  entries_.emplace_back(std::move(key), std::move(value));
}

bool Dictionary::erase(std::string_view key) {
  const auto found =
      std::ranges::find_if(entries_, [&](const Entry& entry) { return entry.first == key; });
  if (found == entries_.end()) {
    return false;
  }
  entries_.erase(found);
  return true;
}

bool operator==(const Dictionary& a, const Dictionary& b) {
  return a.entries_ == b.entries_;
}

}  // namespace pdfkit::syntax
