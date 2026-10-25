// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Dictionary is the PDF dictionary of ISO 32000-1 section 7.3.7 (read): name
// keys mapped to objects, kept in the order the file wrote them, because the
// writer reproduces that order and two runs must write the same bytes. A key
// written twice keeps its first position and its last value, the last-wins
// reading every viewer applies.

#ifndef PDFKIT_SYNTAX_DICTIONARY_HPP
#define PDFKIT_SYNTAX_DICTIONARY_HPP

#include "syntax/object.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::syntax {

class Dictionary {
 public:
  using Entry = std::pair<std::string, Object>;

  Dictionary() = default;

  // The value of key, or nullptr when the key is absent.
  [[nodiscard]] const Object* find(std::string_view key) const noexcept;
  // The value of key, or the null object when the key is absent.
  [[nodiscard]] const Object& get(std::string_view key) const noexcept;
  [[nodiscard]] bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }

  // Sets key: a present key keeps its position and takes the new value.
  void set(std::string key, Object value);
  // Removes key; false when it was absent.
  bool erase(std::string_view key);

  [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

  friend bool operator==(const Dictionary& a, const Dictionary& b);

 private:
  std::vector<Entry> entries_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_DICTIONARY_HPP
