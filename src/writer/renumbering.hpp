// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Renumbering gives the written objects their output numbers, docs/DESIGN.md
// section 17: 1, 2, ... in the first-reach order of the reachability walk
// (the trailer's /Root, then /Info, then depth first in stored order), every
// generation 0, so one input and one edit plan give one numbering whatever
// order the source's table listed its objects in.

#ifndef PDFKIT_WRITER_RENUMBERING_HPP
#define PDFKIT_WRITER_RENUMBERING_HPP

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace pdfkit::writer {

class Renumbering {
 public:
  // order is the reachability walk's first-reach order.
  explicit Renumbering(const std::vector<std::uint32_t>& order);

  // The output number of a graph number; no value when it is not written.
  [[nodiscard]] std::optional<std::uint32_t> find(std::uint32_t number) const;
  // The count of numbered objects.
  [[nodiscard]] std::uint32_t size() const noexcept { return size_; }

 private:
  std::unordered_map<std::uint32_t, std::uint32_t> numbers_;
  std::uint32_t size_ = 0;
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_RENUMBERING_HPP
