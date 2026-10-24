// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Checked addition, multiplication and narrowing for every value read from a
// file. A result that does not fit is a limit_exceeded error naming the value,
// because a wrapped size is how a hostile header turns into an out-of-bounds
// write. The checks use std::numeric_limits only, so they hold on every
// compiler without intrinsics.

#ifndef PDFKIT_PLATFORM_CHECKED_HPP
#define PDFKIT_PLATFORM_CHECKED_HPP

#include "platform/error.hpp"

#include <concepts>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace pdfkit::platform {

// what names the value in the caller's terms, such as "the /Length of object 7".
[[noreturn]] inline void throw_overflow(std::string_view what, std::string_view operation) {
  throw Error(ErrorCode::limit_exceeded,
              std::string(what) + " does not fit in its integer type (" + std::string(operation) +
                  " overflows)",
              "a value that fits the range the format allows",
              "the file is damaged or hostile; re-save it with its producing application, or "
              "report the file if another reader opens it");
}

template <std::integral T>
[[nodiscard]] T checked_add(T a, T b, std::string_view what) {
  constexpr T max = std::numeric_limits<T>::max();
  constexpr T min = std::numeric_limits<T>::min();
  if constexpr (std::is_signed_v<T>) {
    if ((b > 0 && a > max - b) || (b < 0 && a < min - b)) {
      throw_overflow(what, "addition");
    }
  } else {
    if (a > max - b) {
      throw_overflow(what, "addition");
    }
  }
  return static_cast<T>(a + b);
}

template <std::integral T>
[[nodiscard]] T checked_mul(T a, T b, std::string_view what) {
  constexpr T max = std::numeric_limits<T>::max();
  constexpr T min = std::numeric_limits<T>::min();
  if constexpr (std::is_signed_v<T>) {
    bool overflow = false;
    if (a > 0) {
      overflow = b > 0 ? a > max / b : b < min / a;
    } else if (a < 0) {
      overflow = b > 0 ? a < min / b : (b != 0 && b < max / a);
    }
    if (overflow) {
      throw_overflow(what, "multiplication");
    }
  } else {
    if (a != 0 && b > max / a) {
      throw_overflow(what, "multiplication");
    }
  }
  return static_cast<T>(a * b);
}

template <std::integral To, std::integral From>
[[nodiscard]] To checked_narrow(From value, std::string_view what) {
  if (!std::in_range<To>(value)) {
    throw_overflow(what, "conversion");
  }
  return static_cast<To>(value);
}

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_CHECKED_HPP
