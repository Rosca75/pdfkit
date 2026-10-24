// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of platform::Budget: the per-stream and per-document byte limits, the
// object-count limit and the check of a size claimed by the file, each refusing
// with limit_exceeded and a message naming the object, the limit and the way out.

#include "platform/budget.hpp"

#include "platform/error.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>

namespace {

using pdfkit::platform::Budget;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;

TEST_CASE("the default limits are 512 MiB per stream, 2 GiB per document and 8,388,607 objects") {
  const Budget budget;
  CHECK(budget.limits().stream_bytes == 512ULL * 1024 * 1024);
  CHECK(budget.limits().document_bytes == 2048ULL * 1024 * 1024);
  CHECK(budget.limits().objects == 8388607U);
  CHECK(budget.document_used() == 0U);
}

TEST_CASE("a zero limit is refused as an invalid argument") {
  Budget::Limits limits;
  limits.stream_bytes = 0;
  try {
    const Budget budget(limits);
    FAIL("a zero limit was accepted");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::invalid_argument);
  }
}

TEST_CASE("charging counts bytes against the stream and the document") {
  Budget::Limits limits;
  limits.stream_bytes = 100;
  limits.document_bytes = 150;
  Budget budget(limits);
  std::uint64_t first = 0;
  budget.charge(60, first, "object 1");
  budget.charge(40, first, "object 1");
  CHECK(first == 100U);
  CHECK(budget.document_used() == 100U);

  try {
    budget.charge(1, first, "object 1");
    FAIL("the per-stream limit was not enforced");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
    CHECK(error.failed().find("object 1") != std::string::npos);
    CHECK(error.failed().find("100") != std::string::npos);
    CHECK(error.fix().find("stream_bytes") != std::string::npos);
  }

  std::uint64_t second = 0;
  budget.charge(50, second, "object 2");
  try {
    budget.charge(1, second, "object 2");
    FAIL("the per-document limit was not enforced");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
    CHECK(error.fix().find("document_bytes") != std::string::npos);
  }
}

TEST_CASE("an object past the object limit is refused") {
  Budget::Limits limits;
  limits.objects = 2;
  Budget budget(limits);
  budget.count_object("object 1");
  budget.count_object("object 2");
  CHECK(budget.objects_counted() == 2U);
  CHECK_THROWS_AS(budget.count_object("object 3"), Error);
}

TEST_CASE("a size claimed by the file is checked before anything is reserved") {
  Budget::Limits limits;
  limits.stream_bytes = 1000;
  const Budget budget(limits);
  budget.check_claim(1000, "the /Length of object 4");
  try {
    budget.check_claim(1001, "the /Length of object 4");
    FAIL("an oversized claim was accepted");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
    CHECK(error.failed().find("the /Length of object 4") != std::string::npos);
  }
}

}  // namespace
