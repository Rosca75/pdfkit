// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The numbering table. It is a lookup only, never iterated, so the hash map's
// order cannot reach the output.

#include "writer/renumbering.hpp"

namespace pdfkit::writer {

Renumbering::Renumbering(const std::vector<std::uint32_t>& order) {
  numbers_.reserve(order.size());
  for (const std::uint32_t number : order) {
    if (numbers_.emplace(number, size_ + 1).second) {
      ++size_;
    }
  }
}

std::optional<std::uint32_t> Renumbering::find(std::uint32_t number) const {
  const auto found = numbers_.find(number);
  if (found == numbers_.end()) {
    return std::nullopt;
  }
  return found->second;
}

}  // namespace pdfkit::writer
