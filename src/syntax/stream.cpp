// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// How a stream names itself in a message, by the indirect object that holds
// it (the identity a caller can find in the file), and the members that copy
// or compare a stream in either of its forms.

#include "syntax/stream.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace pdfkit::syntax {

Stream Stream::with_dictionary(Dictionary dictionary) const {
  Stream copy = *this;
  copy.dictionary_ = std::move(dictionary);
  return copy;
}

std::string Stream::describe() const {
  if (owns_data()) {
    return "a created stream";
  }
  return "object " + std::to_string(owner_.number) + " " + std::to_string(owner_.generation);
}

bool operator==(const Stream& a, const Stream& b) {
  if (a.dictionary_ != b.dictionary_ || a.data_offset_ != b.data_offset_ ||
      a.data_length_ != b.data_length_ || a.owner_ != b.owner_ ||
      a.owns_data() != b.owns_data()) {
    return false;
  }
  return !a.owns_data() || std::ranges::equal(a.owned_data(), b.owned_data());
}

}  // namespace pdfkit::syntax
