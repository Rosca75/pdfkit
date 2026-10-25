// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The between-line estimate and the grouping. Lines of one direction are
// visited from the top down; each looks only at the lines below it whose top
// lies within reach, so a page of many lines costs little more than its sort.

#include "text/docstrum.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace pdfkit::text {

namespace {

constexpr double bin_length = 10.0;
constexpr double between_line_multiplier = 1.3;
// How far below a line its nearest neighbour is looked for, in its sizes.
constexpr double search_reach = 10.0;

[[nodiscard]] double key(double value) noexcept {
  return std::round(value * 100.0) / 100.0;
}

[[nodiscard]] bool overlap(const Docstrum::Line& a, const Docstrum::Line& b) noexcept {
  return std::min(a.along_high, b.along_high) - std::max(a.along_low, b.along_low) > 0.0;
}

// Line indices per direction, each list sorted from the top down.
[[nodiscard]] std::map<int, std::vector<std::size_t>> by_direction(
    const std::vector<Docstrum::Line>& lines) {
  std::map<int, std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    groups[lines[i].direction].push_back(i);
  }
  for (auto& [direction, items] : groups) {
    std::ranges::stable_sort(items, [&](std::size_t a, std::size_t b) {
      return key(lines[a].across_high) > key(lines[b].across_high);
    });
  }
  return groups;
}

// The mean of the most populated bin (PdfPig's GetPeakAverageDistance).
[[nodiscard]] double peak_average(const std::vector<double>& distances) {
  if (distances.empty()) {
    return 0.0;
  }
  const double largest = std::ceil(*std::ranges::max_element(distances));
  const double length = largest <= 0.0 ? bin_length : std::min(bin_length, largest);
  const auto count = static_cast<std::size_t>(std::ceil(largest / length)) + 1;
  std::vector<std::vector<double>> bins(count);
  for (const double d : distances) {
    const auto at = std::min(static_cast<std::size_t>(std::floor(d / length)), count - 1);
    bins[at].push_back(d);
  }
  std::size_t best = 0;
  for (std::size_t i = 1; i < bins.size(); ++i) {
    if (bins[i].size() > bins[best].size()) {
      best = i;
    }
  }
  return std::accumulate(bins[best].begin(), bins[best].end(), 0.0) /
         static_cast<double>(bins[best].size());
}

class Groups {
 public:
  explicit Groups(std::size_t size) : parent_(size) {
    std::iota(parent_.begin(), parent_.end(), 0);
  }
  std::size_t find(std::size_t at) {
    while (parent_[at] != at) {
      parent_[at] = parent_[parent_[at]];
      at = parent_[at];
    }
    return at;
  }
  void join(std::size_t a, std::size_t b) {
    a = find(a);
    b = find(b);
    if (a != b) {
      parent_[std::max(a, b)] = std::min(a, b);
    }
  }

 private:
  std::vector<std::size_t> parent_;
};

}  // namespace

double Docstrum::between_line_distance(const std::vector<Line>& lines) {
  std::vector<double> distances;
  for (const auto& [direction, items] : by_direction(lines)) {
    for (std::size_t k = 0; k < items.size(); ++k) {
      const Line& upper = lines[items[k]];
      const double reach = search_reach * std::max(upper.size, 1.0);
      for (std::size_t m = k + 1; m < items.size(); ++m) {
        const Line& lower = lines[items[m]];
        const double gap = upper.across_low - lower.across_high;
        if (gap > reach) {
          break;
        }
        if (gap >= 0.0 && overlap(upper, lower)) {
          distances.push_back(gap);
          break;
        }
      }
    }
  }
  double estimate = peak_average(distances);
  if (estimate <= 0.0) {
    std::vector<double> heights;
    for (const Line& line : lines) {
      heights.push_back(line.across_high - line.across_low);
    }
    if (!heights.empty()) {
      std::ranges::sort(heights);
      estimate = heights[heights.size() / 2] / 2.0;
    }
  }
  return estimate;
}

std::vector<std::vector<std::size_t>> Docstrum::blocks(const std::vector<Line>& lines) {
  const double limit = between_line_multiplier * between_line_distance(lines);
  Groups groups(lines.size());
  for (const auto& [direction, items] : by_direction(lines)) {
    for (std::size_t k = 0; k < items.size(); ++k) {
      const Line& upper = lines[items[k]];
      for (std::size_t m = k + 1; m < items.size(); ++m) {
        const Line& lower = lines[items[m]];
        const double gap = upper.across_low - lower.across_high;
        if (gap > limit) {
          break;
        }
        if (overlap(upper, lower)) {
          groups.join(items[k], items[m]);
        }
      }
    }
  }
  std::map<std::size_t, std::vector<std::size_t>> by_root;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    by_root[groups.find(i)].push_back(i);
  }
  std::vector<std::vector<std::size_t>> out;
  out.reserve(by_root.size());
  for (auto& [root, members] : by_root) {
    out.push_back(std::move(members));
  }
  std::ranges::stable_sort(out, {}, [](const std::vector<std::size_t>& g) { return g.front(); });
  return out;
}

}  // namespace pdfkit::text
