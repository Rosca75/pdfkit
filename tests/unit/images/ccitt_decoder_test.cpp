// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::CcittDecoder: round trips through the Group 3 (one- and
// two-dimensional) and Group 4 encoder of ccitt_test_encoder.hpp, the filter
// parameters (/BlackIs1, /EncodedByteAlign, /EndOfLine, /EndOfBlock, /Rows,
// /DamagedRowsBeforeError), a damaged row, a truncated stream, an absurd
// /Columns refused by the budget and known byte vectors worked out from the
// T.4 code tables.

#include "images/ccitt_decoder.hpp"

#include "images/ccitt_test_encoder.hpp"
#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "platform/error.hpp"

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using pdfkit::images::CcittDecoder;
using pdfkit::platform::Budget;
using pdfkit::platform::Error;
using pdfkit::platform::ErrorCode;
using pdfkit::test::BitonalRows;
using pdfkit::test::CcittTestEncoder;

// A deterministic bitmap: stripes, a block, and noise at the given density.
BitonalRows make_rows(std::size_t columns, std::size_t rows, std::uint32_t seed,
                      unsigned density_percent) {
  BitonalRows out(rows, std::vector<bool>(columns, false));
  std::uint32_t state = seed;
  for (std::size_t y = 0; y < rows; ++y) {
    for (std::size_t x = 0; x < columns; ++x) {
      state = state * 1664525U + 1013904223U;
      bool black = ((state >> 24U) % 100U) < density_percent;
      if ((y / 3) % 4 == 1) {
        black = true;
      }
      if (y > 4 && y < 9 && x > 5 && x < columns / 2) {
        black = !black;
      }
      out[y][x] = black;
    }
  }
  return out;
}

CcittDecoder::Result decode(const std::vector<std::byte>& data,
                            CcittDecoder::Parameters parameters) {
  Budget budget;
  return CcittDecoder::decode(data, parameters, budget, "object 7 0");
}

// Whether the pixel (x, y) of the packed output is black under the filter's
// convention: a 0 bit is black unless black_is_1.
bool black_at(const CcittDecoder::Result& result, std::size_t x, std::size_t y, bool black_is_1) {
  const std::size_t row_bytes = (result.columns + 7U) / 8U;
  const auto byte = std::to_integer<unsigned>(result.data[y * row_bytes + x / 8]);
  const bool bit = ((byte >> (7U - x % 8U)) & 1U) != 0;
  return bit == black_is_1;
}

void check_equal(const CcittDecoder::Result& result, const BitonalRows& rows, bool black_is_1,
                 std::size_t row_count) {
  REQUIRE(result.rows >= row_count);
  REQUIRE(result.columns == rows.front().size());
  REQUIRE(result.data.size() >= row_count * ((result.columns + 7U) / 8U));
  for (std::size_t y = 0; y < row_count; ++y) {
    for (std::size_t x = 0; x < rows[y].size(); ++x) {
      if (black_at(result, x, y, black_is_1) != static_cast<bool>(rows[y][x])) {
        FAIL("pixel (" << x << ", " << y << ") differs");
      }
    }
  }
}

CcittDecoder::Parameters group4(std::size_t columns) {
  CcittDecoder::Parameters parameters;
  parameters.k = -1;
  parameters.columns = static_cast<std::int64_t>(columns);
  return parameters;
}

TEST_CASE("known vectors: the codes of T.4 and T.6 decode to the rows they stand for") {
  SUBCASE("Group 4: one vertical-0 code is an all-white row of 8 pixels") {
    const std::vector<std::byte> data = {std::byte{0x80}};
    const auto result = decode(data, group4(8));
    CHECK(result.rows == 1);
    CHECK(result.columns == 8);
    REQUIRE(result.data.size() == 1);
    CHECK(std::to_integer<unsigned>(result.data[0]) == 0xFFU);
  }
  SUBCASE("/BlackIs1 inverts the output bytes") {
    const std::vector<std::byte> data = {std::byte{0x80}};
    auto parameters = group4(8);
    parameters.black_is_1 = true;
    const auto result = decode(data, parameters);
    REQUIRE(result.data.size() == 1);
    CHECK(std::to_integer<unsigned>(result.data[0]) == 0x00U);
  }
  SUBCASE("Group 3 one-dimensional: the white run 8 is 10011") {
    CcittDecoder::Parameters parameters;
    parameters.columns = 8;
    parameters.end_of_block = false;
    parameters.rows = 1;
    const std::vector<std::byte> data = {std::byte{0x98}};
    const auto result = decode(data, parameters);
    CHECK(result.fault.empty());
    REQUIRE(result.data.size() == 1);
    CHECK(std::to_integer<unsigned>(result.data[0]) == 0xFFU);
  }
  SUBCASE("Group 3 one-dimensional: white 2 then black 3 then white 3 is 0111 10 1000") {
    // White 2 = 0111, black 3 = 10, white 3 = 1000: 0111 1010 00.
    CcittDecoder::Parameters parameters;
    parameters.columns = 8;
    parameters.rows = 1;
    parameters.end_of_block = false;
    const std::vector<std::byte> data = {std::byte{0x7A}, std::byte{0x00}};
    const auto result = decode(data, parameters);
    REQUIRE(result.data.size() == 1);
    // Pixels: 2 white, 3 black, 3 white = 11000111.
    CHECK(std::to_integer<unsigned>(result.data[0]) == 0xC7U);
  }
  SUBCASE("the run-length tables hold the codes the standard lists") {
    CHECK(CcittTestEncoder::run_code(false, 0) == "00110101");
    CHECK(CcittTestEncoder::run_code(false, 1) == "000111");
    CHECK(CcittTestEncoder::run_code(false, 63) == "00110100");
    CHECK(CcittTestEncoder::run_code(false, 64) ==
          "11011"
          "00110101");
    CHECK(CcittTestEncoder::run_code(true, 0) == "0000110111");
    CHECK(CcittTestEncoder::run_code(true, 2) == "11");
    CHECK(CcittTestEncoder::run_code(true, 63) == "000001100111");
    CHECK(CcittTestEncoder::run_code(true, 64) ==
          "0000001111"
          "0000110111");
  }
}

TEST_CASE("Group 4 round trips, with and without the end-of-block code") {
  for (const std::size_t columns : {1U, 7U, 8U, 37U, 200U}) {
    for (const unsigned density : {0U, 5U, 50U, 100U}) {
      const BitonalRows rows =
          make_rows(columns, 24, 11 + static_cast<std::uint32_t>(columns), density);
      for (const bool end_of_block : {true, false}) {
        CcittTestEncoder::Options options;
        options.k = -1;
        options.end_of_block = end_of_block;
        const auto data = CcittTestEncoder::encode(rows, options);
        auto parameters = group4(columns);
        parameters.end_of_block = end_of_block;
        parameters.rows = static_cast<std::int64_t>(rows.size());
        const auto result = decode(data, parameters);
        INFO("columns " << columns << " density " << density << " eob " << end_of_block);
        CHECK(result.fault.empty());
        CHECK(result.rows == rows.size());
        CHECK(result.damaged_rows == 0);
        check_equal(result, rows, false, rows.size());
      }
    }
  }
}

TEST_CASE("Group 4 with no /Rows stops at the end-of-block code") {
  const BitonalRows rows = make_rows(64, 17, 3, 20);
  const auto data = CcittTestEncoder::encode(rows, {});
  const auto result = decode(data, group4(64));
  CHECK(result.fault.empty());
  CHECK(result.rows == 17);
  check_equal(result, rows, false, 17);
}

TEST_CASE("/BlackIs1 flips the output of a Group 4 stream") {
  const BitonalRows rows = make_rows(45, 12, 5, 30);
  const auto data = CcittTestEncoder::encode(rows, {});
  auto parameters = group4(45);
  parameters.black_is_1 = true;
  const auto result = decode(data, parameters);
  check_equal(result, rows, true, rows.size());
}

TEST_CASE("Group 3 one-dimensional round trips with and without end-of-line codes") {
  for (const bool end_of_line : {false, true}) {
    for (const bool end_of_block : {false, true}) {
      for (const std::size_t columns : {8U, 37U, 1728U, 5000U}) {
        const BitonalRows rows = make_rows(columns, 10, 21, 10);
        CcittTestEncoder::Options options;
        options.k = 0;
        options.end_of_line = end_of_line;
        options.end_of_block = end_of_block && end_of_line;
        const auto data = CcittTestEncoder::encode(rows, options);
        CcittDecoder::Parameters parameters;
        parameters.k = 0;
        parameters.columns = static_cast<std::int64_t>(columns);
        parameters.end_of_line = end_of_line;
        parameters.rows = static_cast<std::int64_t>(rows.size());
        parameters.end_of_block = end_of_block;
        const auto result = decode(data, parameters);
        INFO("columns " << columns << " eol " << end_of_line << " eob " << end_of_block);
        CHECK(result.fault.empty());
        CHECK(result.rows == rows.size());
        check_equal(result, rows, false, rows.size());
      }
    }
  }
}

TEST_CASE("Group 3 one-dimensional stops at the return-to-control code") {
  const BitonalRows rows = make_rows(100, 9, 8, 15);
  CcittTestEncoder::Options options;
  options.k = 0;
  options.end_of_line = true;
  const auto data = CcittTestEncoder::encode(rows, options);
  CcittDecoder::Parameters parameters;
  parameters.columns = 100;
  parameters.end_of_line = true;
  const auto result = decode(data, parameters);
  CHECK(result.fault.empty());
  CHECK(result.rows == 9);
  check_equal(result, rows, false, 9);
}

TEST_CASE("Group 3 two-dimensional round trips") {
  for (const int k : {2, 4}) {
    for (const std::size_t columns : {16U, 37U, 300U}) {
      const BitonalRows rows = make_rows(columns, 13, 31, 12);
      CcittTestEncoder::Options options;
      options.k = k;
      const auto data = CcittTestEncoder::encode(rows, options);
      CcittDecoder::Parameters parameters;
      parameters.k = k;
      parameters.columns = static_cast<std::int64_t>(columns);
      parameters.end_of_line = true;
      const auto result = decode(data, parameters);
      INFO("k " << k << " columns " << columns);
      CHECK(result.fault.empty());
      CHECK(result.rows == rows.size());
      check_equal(result, rows, false, rows.size());
    }
  }
}

TEST_CASE("/EncodedByteAlign: every row starts on a byte boundary") {
  const BitonalRows rows = make_rows(53, 11, 17, 25);
  SUBCASE("Group 4") {
    CcittTestEncoder::Options options;
    options.k = -1;
    options.byte_align = true;
    const auto data = CcittTestEncoder::encode(rows, options);
    auto parameters = group4(53);
    parameters.encoded_byte_align = true;
    const auto result = decode(data, parameters);
    CHECK(result.rows == rows.size());
    check_equal(result, rows, false, rows.size());
  }
  SUBCASE("Group 3 one-dimensional without end-of-line codes") {
    CcittTestEncoder::Options options;
    options.k = 0;
    options.byte_align = true;
    options.end_of_block = false;
    const auto data = CcittTestEncoder::encode(rows, options);
    CcittDecoder::Parameters parameters;
    parameters.columns = 53;
    parameters.encoded_byte_align = true;
    parameters.end_of_block = false;
    parameters.rows = static_cast<std::int64_t>(rows.size());
    const auto result = decode(data, parameters);
    CHECK(result.rows == rows.size());
    check_equal(result, rows, false, rows.size());
  }
  SUBCASE("Group 3 one-dimensional with byte-aligned end-of-line codes") {
    CcittTestEncoder::Options options;
    options.k = 0;
    options.byte_align = true;
    options.end_of_line = true;
    options.end_of_block = false;
    const auto data = CcittTestEncoder::encode(rows, options);
    CcittDecoder::Parameters parameters;
    parameters.columns = 53;
    parameters.encoded_byte_align = true;
    parameters.end_of_line = true;
    parameters.end_of_block = false;
    parameters.rows = static_cast<std::int64_t>(rows.size());
    const auto result = decode(data, parameters);
    CHECK(result.rows == rows.size());
    check_equal(result, rows, false, rows.size());
  }
}

TEST_CASE("/Rows ends the decoding before the data does") {
  const BitonalRows rows = make_rows(40, 10, 9, 20);
  const auto data = CcittTestEncoder::encode(rows, {});
  auto parameters = group4(40);
  parameters.rows = 6;
  const auto result = decode(data, parameters);
  CHECK(result.fault.empty());
  CHECK(result.rows == 6);
  CHECK(result.data.size() == 6 * 5);
  check_equal(result, rows, false, 6);
}

TEST_CASE("bytes_consumed ends after the end-of-block code, so streams can follow each other") {
  const BitonalRows first = make_rows(50, 9, 13, 30);
  const BitonalRows second = make_rows(50, 7, 14, 30);
  auto data = CcittTestEncoder::encode(first, {});
  const std::size_t first_size = data.size();
  const auto more = CcittTestEncoder::encode(second, {});
  data.insert(data.end(), more.begin(), more.end());

  auto parameters = group4(50);
  parameters.rows = 9;
  const auto one = decode(data, parameters);
  CHECK(one.fault.empty());
  CHECK(one.bytes_consumed == first_size);
  check_equal(one, first, false, 9);

  parameters.rows = 7;
  const std::vector<std::byte> rest(data.begin() + static_cast<std::ptrdiff_t>(one.bytes_consumed),
                                    data.end());
  const auto two = decode(rest, parameters);
  CHECK(two.fault.empty());
  CHECK(two.bytes_consumed == more.size());
  check_equal(two, second, false, 7);
}
TEST_CASE("data that holds fewer rows than /Rows is a fault that keeps the rows it has") {
  const BitonalRows rows = make_rows(40, 5, 9, 20);
  const auto data = CcittTestEncoder::encode(rows, {});
  auto parameters = group4(40);
  parameters.rows = 9;
  const auto result = decode(data, parameters);
  CHECK_FALSE(result.fault.empty());
  CHECK(result.rows == 5);
  check_equal(result, rows, false, 5);
}

TEST_CASE("a truncated stream is a fault that keeps the rows before the cut") {
  const BitonalRows rows = make_rows(120, 30, 4, 30);
  CcittTestEncoder::Options options;
  options.end_of_block = false;
  auto data = CcittTestEncoder::encode(rows, options);
  data.resize(data.size() / 2);
  auto parameters = group4(120);
  parameters.rows = 30;
  parameters.end_of_block = true;
  const auto result = decode(data, parameters);
  CHECK_FALSE(result.fault.empty());
  CHECK(result.rows > 3);
  CHECK(result.rows < 30);
  // The last row decoded may be cut inside; every row before it is exact.
  check_equal(result, rows, false, result.rows - 1);
}

TEST_CASE("a damaged row is counted, and read as white under /DamagedRowsBeforeError") {
  // Group 3 one-dimensional rows, each opened by an end-of-line code. Row 2's
  // black run is longer than the row, so its length is wrong. As pdf.js and
  // xpdf do, the decoder then skips to the next end-of-line code, which swallows
  // row 3, so the output holds the source rows 0, 1, 2 (damaged), 4 and 5.
  const std::size_t columns = 40;
  const BitonalRows rows = make_rows(columns, 6, 77, 20);
  std::string bits;
  const std::string eol = "000000000001";
  for (std::size_t y = 0; y < rows.size(); ++y) {
    bits += eol;
    if (y == 2) {
      bits += CcittTestEncoder::run_code(false, 5);
      bits += CcittTestEncoder::run_code(true, columns + 30);
      continue;
    }
    bool colour = false;
    std::size_t x = 0;
    while (x < columns) {
      std::size_t end = x;
      while (end < columns && rows[y][end] == colour) {
        ++end;
      }
      bits += CcittTestEncoder::run_code(colour, end - x);
      x = end;
      colour = !colour;
    }
  }
  bits += eol;
  const auto data = CcittTestEncoder::pack(bits);
  CcittDecoder::Parameters parameters;
  parameters.columns = static_cast<std::int64_t>(columns);
  parameters.end_of_line = true;

  const auto expect_surrounding_rows = [&](const CcittDecoder::Result& result) {
    CHECK(result.fault.empty());
    CHECK(result.rows == 5);
    CHECK(result.damaged_rows == 1);
    REQUIRE(result.data.size() == 5 * 5);
    check_equal(result, rows, false, 2);
    const std::size_t source_rows[] = {4, 5};
    for (std::size_t i = 0; i < 2; ++i) {
      for (std::size_t x = 0; x < columns; ++x) {
        CHECK(black_at(result, x, 3 + i, false) == static_cast<bool>(rows[source_rows[i]][x]));
      }
    }
  };

  SUBCASE("the damaged row keeps what decoded before the error") {
    const auto result = decode(data, parameters);
    expect_surrounding_rows(result);
    for (std::size_t x = 0; x < columns; ++x) {
      CHECK(black_at(result, x, 2, false) == (x >= 5));
    }
  }
  SUBCASE("/DamagedRowsBeforeError makes the damaged row white") {
    parameters.damaged_rows_before_error = 2;
    const auto result = decode(data, parameters);
    expect_surrounding_rows(result);
    for (std::size_t x = 0; x < columns; ++x) {
      CHECK_FALSE(black_at(result, x, 2, false));
    }
  }
}

TEST_CASE("garbage in place of a Group 4 stream never crashes or runs on") {
  std::vector<std::byte> data(4096);
  std::uint32_t state = 99;
  for (std::byte& value : data) {
    state = state * 1103515245U + 12345U;
    value = static_cast<std::byte>(state >> 16U);
  }
  for (const std::int64_t k : {-1, 0, 3}) {
    for (const bool end_of_block : {true, false}) {
      CcittDecoder::Parameters parameters;
      parameters.k = k;
      parameters.columns = 211;
      parameters.end_of_block = end_of_block;
      parameters.end_of_line = (k == 0);
      const auto result = decode(data, parameters);
      CHECK(result.data.size() == static_cast<std::size_t>(result.rows) * 27);
    }
  }
}

TEST_CASE("an absurd /Columns is refused by the budget before anything is reserved") {
  const std::vector<std::byte> data = {std::byte{0x80}};
  auto parameters = group4(1);
  parameters.columns = 1'099'511'627'776;
  Budget budget;
  try {
    const auto result = CcittDecoder::decode(data, parameters, budget, "object 7 0");
    FAIL("the claim was accepted");
  } catch (const Error& error) {
    CHECK(error.code() == ErrorCode::limit_exceeded);
  }
}

TEST_CASE("the output is charged to the budget") {
  const BitonalRows rows = make_rows(64, 100, 6, 20);
  const auto data = CcittTestEncoder::encode(rows, {});
  Budget::Limits limits;
  limits.stream_bytes = 100;
  Budget budget(limits);
  CHECK_THROWS_AS(
      const auto discarded = CcittDecoder::decode(data, group4(64), budget, "object 7 0"), Error);
}

TEST_CASE("a non-positive /Columns is a fault") {
  const std::vector<std::byte> data = {std::byte{0x80}};
  auto parameters = group4(1);
  parameters.columns = 0;
  const auto result = decode(data, parameters);
  CHECK_FALSE(result.fault.empty());
  CHECK(result.rows == 0);
  CHECK(result.data.empty());
}

}  // namespace
