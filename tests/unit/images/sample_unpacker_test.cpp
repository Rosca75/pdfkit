// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::SampleUnpacker: each bit depth with a row padded to its
// byte boundary, a short row read as zeros, and /Decode applied (the
// inverted [1 0] of a stencil among them).

#include "images/sample_unpacker.hpp"

#include "images/image_fixture.hpp"
#include "platform/byte_span.hpp"

#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::images::SampleUnpacker;
using pdfkit::platform::as_bytes;
using pdfkit::test::bytes_of;

std::vector<std::uint16_t> unpack(std::uint32_t bits, std::size_t count, const std::string& row) {
  const SampleUnpacker unpacker(bits, count, 1, {});
  std::vector<std::uint16_t> out(count);
  unpacker.unpack(as_bytes(row), out);
  return out;
}

TEST_CASE("each bit depth unpacks most significant bits first") {
  CHECK(unpack(1, 10, bytes_of({0xA5, 0xC0})) ==
        std::vector<std::uint16_t>{1, 0, 1, 0, 0, 1, 0, 1, 1, 1});
  CHECK(unpack(2, 5, bytes_of({0x1B, 0x80})) == std::vector<std::uint16_t>{0, 1, 2, 3, 2});
  CHECK(unpack(4, 3, bytes_of({0x9F, 0x30})) == std::vector<std::uint16_t>{9, 15, 3});
  CHECK(unpack(8, 3, bytes_of({0, 128, 255})) == std::vector<std::uint16_t>{0, 128, 255});
  CHECK(unpack(16, 2, bytes_of({0x12, 0x34, 0xFF, 0xFE})) ==
        std::vector<std::uint16_t>{0x1234, 0xFFFE});
}

TEST_CASE("a row is padded to a whole byte and a short row reads zeros") {
  const SampleUnpacker unpacker(1, 10, 1, {});
  CHECK(unpacker.row_bytes() == 2U);
  CHECK(SampleUnpacker(4, 3, 1, {}).row_bytes() == 2U);
  CHECK(SampleUnpacker(8, 6, 3, {}).row_bytes() == 6U);
  CHECK(unpack(8, 4, bytes_of({7})) == std::vector<std::uint16_t>{7, 0, 0, 0});
}

TEST_CASE("decode maps a raw value through the component's pair") {
  const SampleUnpacker plain(8, 1, 1, {});
  CHECK(plain.identity_decode());
  CHECK(plain.decoded(255, 0) == doctest::Approx(1.0));
  CHECK(plain.decoded(0, 0) == doctest::Approx(0.0));
  const SampleUnpacker inverted(1, 1, 1, {1.0, 0.0});
  CHECK_FALSE(inverted.identity_decode());
  CHECK(inverted.decoded(0, 0) == doctest::Approx(1.0));
  CHECK(inverted.decoded(1, 0) == doctest::Approx(0.0));
  const SampleUnpacker indexed(4, 1, 1, {0.0, 15.0});
  CHECK(indexed.decoded(9, 0) == doctest::Approx(9.0));
  const SampleUnpacker two(8, 2, 2, {0.0, 1.0, 1.0, 0.5});
  CHECK(two.decoded(255, 1) == doctest::Approx(0.5));
  // A /Decode of the wrong length is [0 1] per component.
  const SampleUnpacker wrong(8, 3, 3, {1.0, 0.0});
  CHECK(wrong.identity_decode());
}

}  // namespace
