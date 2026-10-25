// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of images::ArithmeticDecoder: the test sequence of ITU-T T.88 annex H.2
// (recalled), round trips through the MQ encoder of mq_test_encoder.hpp over
// skewed and uniform decisions under several contexts, and the fill a stream
// that ends early reads.

#include "images/arithmetic_decoder.hpp"

#include "images/mq_test_encoder.hpp"

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using pdfkit::images::ArithmeticDecoder;
using pdfkit::test::MqTestEncoder;

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

TEST_CASE("the annex H.2 test sequence decodes from its encoded bytes") {
  // 256 decisions under a single context.
  const auto encoded = bytes({0x84, 0xC7, 0x3B, 0xFC, 0xE1, 0xA1, 0x43, 0x04, 0x02, 0x20,
                              0x00, 0x00, 0x41, 0x0D, 0xBB, 0x86, 0xF4, 0x31, 0x7F, 0xFF,
                              0x88, 0xFF, 0x37, 0x47, 0x1A, 0xDB, 0x6A, 0xDF, 0xFF, 0xAC});
  const auto expected = bytes({0x00, 0x02, 0x00, 0x51, 0x00, 0x00, 0x00, 0xC0, 0x03, 0x52, 0x87,
                               0x2A, 0xAA, 0xAA, 0xAA, 0xAA, 0x82, 0xC0, 0x20, 0x00, 0xFC, 0xD7,
                               0x9E, 0xF6, 0xBF, 0x7F, 0xED, 0x90, 0x4F, 0x46, 0xA3, 0xBF});
  ArithmeticDecoder decoder(encoded);
  std::uint8_t context = 0;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    unsigned value = 0;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value << 1U) | decoder.read_bit(context);
    }
    INFO("byte " << i);
    CHECK(value == std::to_integer<unsigned>(expected[i]));
  }
}

TEST_CASE("the encoder reproduces the annex H.2 encoded bytes") {
  const auto expected = bytes({0x00, 0x02, 0x00, 0x51, 0x00, 0x00, 0x00, 0xC0, 0x03, 0x52, 0x87,
                               0x2A, 0xAA, 0xAA, 0xAA, 0xAA, 0x82, 0xC0, 0x20, 0x00, 0xFC, 0xD7,
                               0x9E, 0xF6, 0xBF, 0x7F, 0xED, 0x90, 0x4F, 0x46, 0xA3, 0xBF});
  MqTestEncoder encoder;
  std::uint8_t context = 0;
  for (const std::byte value : expected) {
    for (unsigned bit = 8; bit > 0; --bit) {
      encoder.encode(context, (std::to_integer<unsigned>(value) >> (bit - 1U)) & 1U);
    }
  }
  const auto encoded = encoder.finish();
  const auto reference = bytes({0x84, 0xC7, 0x3B, 0xFC, 0xE1, 0xA1, 0x43, 0x04, 0x02, 0x20,
                                0x00, 0x00, 0x41, 0x0D, 0xBB, 0x86, 0xF4, 0x31, 0x7F, 0xFF,
                                0x88, 0xFF, 0x37, 0x47, 0x1A, 0xDB, 0x6A, 0xDF, 0xFF, 0xAC});
  CHECK(encoded == reference);
}

TEST_CASE("decisions of any skew round trip through the encoder under several contexts") {
  for (const unsigned skew : {1U, 10U, 50U, 90U, 99U}) {
    std::vector<unsigned> decisions;
    std::vector<std::size_t> which;
    std::uint32_t state = 7 + skew;
    for (int i = 0; i < 20000; ++i) {
      state = state * 1664525U + 1013904223U;
      which.push_back((state >> 8U) % 5U);
      decisions.push_back(((state >> 20U) % 100U) < skew ? 1U : 0U);
    }
    std::vector<std::uint8_t> encode_contexts(5, 0);
    MqTestEncoder encoder;
    for (std::size_t i = 0; i < decisions.size(); ++i) {
      encoder.encode(encode_contexts[which[i]], decisions[i]);
    }
    const auto encoded = encoder.finish();
    std::vector<std::uint8_t> decode_contexts(5, 0);
    ArithmeticDecoder decoder(encoded);
    bool equal = true;
    for (std::size_t i = 0; i < decisions.size() && equal; ++i) {
      equal = decoder.read_bit(decode_contexts[which[i]]) == decisions[i];
    }
    INFO("skew " << skew);
    CHECK(equal);
    CHECK(encode_contexts == decode_contexts);
  }
}

TEST_CASE("a stream that ends early reads on from the fill and never outside its data") {
  const std::vector<std::byte> empty;
  ArithmeticDecoder decoder(empty);
  std::uint8_t context = 0;
  for (int i = 0; i < 100000; ++i) {
    (void)decoder.read_bit(context);
  }
  CHECK(context < 94);
}

}  // namespace
