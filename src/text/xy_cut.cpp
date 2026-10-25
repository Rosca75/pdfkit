// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The recursive cut. Recursion depth is bounded by the number of boxes, as
// every cut makes each part strictly smaller; an explicit work list keeps the
// stack flat for a page of many thousands of blocks.

#include "text/xy_cut.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace pdfkit::text {

namespace {

[[nodiscard]] double key(double value) noexcept {
  return std::round(value * 100.0) / 100.0;
}

// The groups of indices whose projections on one axis are separated by a
// gap, in increasing order of the axis.
[[nodiscard]] std::vector<std::vector<std::size_t>> split(const std::vector<Rect>& boxes,
                                                          std::vector<std::size_t> items,
                                                          bool horizontal) {
  const auto low = [&](std::size_t i) {
    return key(horizontal ? boxes[i].left : boxes[i].bottom);
  };
  const auto high = [&](std::size_t i) {
    return key(horizontal ? boxes[i].right : boxes[i].top);
  };
  std::ranges::stable_sort(items, [&](std::size_t a, std::size_t b) { return low(a) < low(b); });
  std::vector<std::vector<std::size_t>> groups;
  double reach = 0.0;
  for (const std::size_t item : items) {
    if (groups.empty() || low(item) > reach) {
      groups.emplace_back();
      reach = high(item);
    } else {
      reach = std::max(reach, high(item));
    }
    groups.back().push_back(item);
  }
  return groups;
}

}  // namespace

std::vector<std::size_t> XyCut::order(const std::vector<Rect>& boxes, bool right_to_left) {
  std::vector<std::size_t> out;
  out.reserve(boxes.size());
  std::vector<std::size_t> all(boxes.size());
  for (std::size_t i = 0; i < boxes.size(); ++i) {
    all[i] = i;
  }
  // A work list of nodes, the next to read last.
  std::vector<std::vector<std::size_t>> work{std::move(all)};
  while (!work.empty()) {
    std::vector<std::size_t> node = std::move(work.back());
    work.pop_back();
    if (node.size() <= 1) {
      out.insert(out.end(), node.begin(), node.end());
      continue;
    }
    std::vector<std::vector<std::size_t>> parts = split(boxes, node, true);
    if (parts.size() > 1) {
      if (right_to_left) {
        std::ranges::reverse(parts);
      }
    } else {
      parts = split(boxes, node, false);
      // Top to bottom: the highest projection first.
      std::ranges::reverse(parts);
    }
    if (parts.size() > 1) {
      for (std::vector<std::size_t>& part : std::views::reverse(parts)) {
        work.push_back(std::move(part));
      }
      continue;
    }
    // No gap on either axis: top to bottom, then left to right.
    std::ranges::stable_sort(node, [&](std::size_t a, std::size_t b) {
      const double ta = key(boxes[a].top);
      const double tb = key(boxes[b].top);
      if (ta != tb) {
        return ta > tb;
      }
      return right_to_left ? key(boxes[a].right) > key(boxes[b].right)
                           : key(boxes[a].left) < key(boxes[b].left);
    });
    out.insert(out.end(), node.begin(), node.end());
  }
  return out;
}

}  // namespace pdfkit::text
