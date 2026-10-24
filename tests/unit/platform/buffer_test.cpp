// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of platform::Buffer and the ByteSpan readers: a buffer's growth is
// charged to its budget as one stream, and a read past the end of a span
// yields nothing rather than a byte from elsewhere.

#include "platform/buffer.hpp"

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "platform/error.hpp"

#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using pdfkit::platform::Budget;
using pdfkit::platform::Buffer;
using pdfkit::platform::ByteSpan;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;

TEST_CASE("a buffer appends bytes and charges its growth to the budget") {
  Budget budget;
  Buffer buffer(budget, "object 3");
  buffer.append(pdfkit::platform::as_bytes("abc"));
  buffer.push_back(std::byte{'d'});
  CHECK(buffer.size() == 4U);
  CHECK(budget.document_used() == 4U);
  const ByteSpan view = buffer.view();
  CHECK(view.size() == 4U);
  CHECK(view[3] == std::byte{'d'});
  const std::vector<std::byte> bytes = std::move(buffer).release();
  CHECK(bytes.size() == 4U);
}

TEST_CASE("a buffer refuses growth past the per-stream limit") {
  Budget::Limits limits;
  limits.stream_bytes = 4;
  Budget budget(limits);
  Buffer buffer(budget, "object 9");
  buffer.append(pdfkit::platform::as_bytes("abcd"));
  try {
    buffer.push_back(std::byte{'e'});
    FAIL("the buffer grew past its limit");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
    CHECK(error.failed().find("object 9") != std::string::npos);
  }
}

TEST_CASE("reserving checks the claim without charging it") {
  Budget::Limits limits;
  limits.stream_bytes = 8;
  Budget budget(limits);
  Buffer buffer(budget, "object 2");
  buffer.reserve(8);
  CHECK(budget.document_used() == 0U);
  CHECK_THROWS_AS(buffer.reserve(9), Error);
}

TEST_CASE("the span readers read big- and little-endian integers within bounds") {
  const std::array<std::byte, 5> raw{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                     std::byte{0x04}, std::byte{0x05}};
  const ByteSpan span(raw);
  CHECK(pdfkit::platform::read_be<std::uint16_t>(span, 0) == std::uint16_t{0x0102});
  CHECK(pdfkit::platform::read_le<std::uint16_t>(span, 0) == std::uint16_t{0x0201});
  CHECK(pdfkit::platform::read_be<std::uint32_t>(span, 1) == std::uint32_t{0x02030405});
  CHECK(pdfkit::platform::read_le<std::uint32_t>(span, 1) == std::uint32_t{0x05040302});
  CHECK(pdfkit::platform::read_be<std::uint8_t>(span, 4) == std::uint8_t{0x05});
  CHECK_FALSE(pdfkit::platform::read_be<std::uint32_t>(span, 2).has_value());
  CHECK_FALSE(pdfkit::platform::read_le<std::uint16_t>(span, 4).has_value());
  CHECK_FALSE(pdfkit::platform::read_be<std::uint8_t>(span, 5).has_value());
  CHECK_FALSE(pdfkit::platform::read_be<std::uint8_t>(span, SIZE_MAX).has_value());
}

}  // namespace
