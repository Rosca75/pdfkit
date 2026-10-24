// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the checked arithmetic every value read from a file goes through:
// an overflow or a lossy narrowing is a limit_exceeded error naming the value,
// never a wrapped number.

#include "platform/checked.hpp"

#include "platform/error.hpp"

#include <doctest.h>

#include <cstdint>
#include <limits>

namespace {

using pdfkit::platform::checked_add;
using pdfkit::platform::checked_mul;
using pdfkit::platform::checked_narrow;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;

template <typename Fn>
ErrorCode code_of(Fn&& fn) {
  try {
    fn();
  } catch (const Error& error) {
    return error.code();
  }
  return ErrorCode::internal;
}

TEST_CASE("checked addition returns the sum or refuses an overflow") {
  CHECK(checked_add<std::uint32_t>(1, 2, "a length") == 3U);
  CHECK(checked_add<std::int32_t>(-5, 3, "an offset") == -2);
  const auto u_max = std::numeric_limits<std::uint64_t>::max();
  CHECK(code_of([&] { (void)checked_add<std::uint64_t>(u_max, 1, "a length"); }) ==
        ErrorCode::limit_exceeded);
  const auto i_max = std::numeric_limits<std::int64_t>::max();
  const auto i_min = std::numeric_limits<std::int64_t>::min();
  CHECK(code_of([&] { (void)checked_add<std::int64_t>(i_max, 1, "an offset"); }) ==
        ErrorCode::limit_exceeded);
  CHECK(code_of([&] { (void)checked_add<std::int64_t>(i_min, -1, "an offset"); }) ==
        ErrorCode::limit_exceeded);
  CHECK(checked_add<std::int64_t>(i_min, i_max, "an offset") == -1);
}

TEST_CASE("checked multiplication returns the product or refuses an overflow") {
  CHECK(checked_mul<std::uint32_t>(1000, 1000, "a pixel count") == 1000000U);
  CHECK(checked_mul<std::uint32_t>(0, 7, "a pixel count") == 0U);
  CHECK(checked_mul<std::int32_t>(-4, 5, "a size") == -20);
  CHECK(checked_mul<std::int32_t>(-4, -5, "a size") == 20);
  CHECK(code_of([] { (void)checked_mul<std::uint32_t>(65536, 65536, "a pixel count"); }) ==
        ErrorCode::limit_exceeded);
  const auto i_min = std::numeric_limits<std::int32_t>::min();
  CHECK(code_of([&] { (void)checked_mul<std::int32_t>(i_min, -1, "a size"); }) ==
        ErrorCode::limit_exceeded);
  CHECK(code_of([&] { (void)checked_mul<std::int32_t>(-65536, 65536, "a size"); }) ==
        ErrorCode::limit_exceeded);
  CHECK(code_of([&] { (void)checked_mul<std::int32_t>(65536, -65536, "a size"); }) ==
        ErrorCode::limit_exceeded);
}

TEST_CASE("checked narrowing keeps the value or refuses a lossy conversion") {
  CHECK(checked_narrow<std::uint8_t>(255, "a component") == 255);
  CHECK(checked_narrow<std::int16_t>(std::int64_t{-32768}, "a delta") == -32768);
  CHECK(code_of([] { (void)checked_narrow<std::uint8_t>(256, "a component"); }) ==
        ErrorCode::limit_exceeded);
  CHECK(code_of([] { (void)checked_narrow<std::uint32_t>(-1, "a count"); }) ==
        ErrorCode::limit_exceeded);
  CHECK(code_of([] {
          (void)checked_narrow<std::int64_t>(std::numeric_limits<std::uint64_t>::max(), "a size");
        }) == ErrorCode::limit_exceeded);
}

TEST_CASE("an overflow error names the value and the way out") {
  try {
    (void)checked_add<std::uint8_t>(200, 100, "the /Length of object 7");
    FAIL("no error was raised");
  } catch (const Error& error) {
    CHECK(error.failed().find("the /Length of object 7") != std::string::npos);
    CHECK_FALSE(error.expected().empty());
    CHECK_FALSE(error.fix().empty());
  }
}

}  // namespace
