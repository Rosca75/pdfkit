// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// require_value fails the running test when an optional is empty and returns
// its value otherwise. The explicit check before the access is what lets the
// static analysis prove every access to an optional checked, which doctest's
// REQUIRE alone does not show it.

#ifndef PDFKIT_TESTS_SUPPORT_REQUIRE_VALUE_HPP
#define PDFKIT_TESTS_SUPPORT_REQUIRE_VALUE_HPP

#include <doctest.h>

#include <optional>
#include <utility>

namespace pdfkit::test {

template <typename T>
[[nodiscard]] T require_value(std::optional<T> value) {
  REQUIRE(value.has_value());
  if (!value.has_value()) {
    return T{};
  }
  return std::move(*value);
}

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_SUPPORT_REQUIRE_VALUE_HPP
