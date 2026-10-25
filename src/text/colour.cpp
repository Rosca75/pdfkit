// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The initial colours and the white test. A component within a thousandth of
// its white value counts as white, because producers write 0.999 as often as
// 1.

#include "text/colour.hpp"

#include <algorithm>

namespace pdfkit::text {

namespace {

constexpr double near = 0.001;

[[nodiscard]] bool all_at_least(const std::vector<double>& values, double floor) {
  return !values.empty() && std::ranges::all_of(values, [floor](double v) { return v >= floor; });
}

[[nodiscard]] bool all_at_most(const std::vector<double>& values, double ceiling) {
  return !values.empty() &&
         std::ranges::all_of(values, [ceiling](double v) { return v <= ceiling; });
}

}  // namespace

void Colour::reset() {
  if (family == "DeviceCMYK" || (family == "ICCBased" && count == 4)) {
    components.assign(4, 0.0);
    components[3] = 1.0;
  } else if (family == "DeviceRGB" || family == "CalRGB" || family == "Lab" ||
             (family == "ICCBased" && count == 3)) {
    components.assign(3, 0.0);
  } else if (family == "Separation" || family == "DeviceN") {
    components.assign(static_cast<std::size_t>(std::max(count, 1)), 1.0);
  } else if (family == "Pattern") {
    components.clear();
  } else {
    components.assign(1, 0.0);
  }
}

bool Colour::is_white() const noexcept {
  if (family == "DeviceGray" || family == "CalGray" || (family == "ICCBased" && count == 1)) {
    return components.size() == 1 && components[0] >= 1.0 - near;
  }
  if (family == "DeviceRGB" || family == "CalRGB" || (family == "ICCBased" && count == 3)) {
    return components.size() == 3 && all_at_least(components, 1.0 - near);
  }
  if (family == "DeviceCMYK" || (family == "ICCBased" && count == 4)) {
    return components.size() == 4 && all_at_most(components, near);
  }
  if (family == "Separation" || family == "DeviceN") {
    return all_at_most(components, near);
  }
  if (family == "Lab") {
    return !components.empty() && components[0] >= 100.0 - near;
  }
  return false;
}

}  // namespace pdfkit::text
