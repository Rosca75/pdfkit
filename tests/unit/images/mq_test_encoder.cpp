// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The MQ encoder of ITU-T T.88 annex E.2 (INITENC, ENCODE, CODEMPS, CODELPS,
// RENORME, BYTEOUT and FLUSH, recalled from the standard's flowcharts) and the
// integer encoding of annex A. The probability estimation table is Table E.1.

#include "images/mq_test_encoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pdfkit::test {

namespace {

struct Qe {
  std::uint32_t qe;
  std::uint8_t nmps;
  std::uint8_t nlps;
  std::uint8_t sw;
};

constexpr std::array<Qe, 47> k_table = {{
    {0x5601, 1, 1, 1},   {0x3401, 2, 6, 0},   {0x1801, 3, 9, 0},   {0x0AC1, 4, 12, 0},
    {0x0521, 5, 29, 0},  {0x0221, 38, 33, 0}, {0x5601, 7, 6, 1},   {0x5401, 8, 14, 0},
    {0x4801, 9, 14, 0},  {0x3801, 10, 14, 0}, {0x3001, 11, 17, 0}, {0x2401, 12, 18, 0},
    {0x1C01, 13, 20, 0}, {0x1601, 29, 21, 0}, {0x5601, 15, 14, 1}, {0x5401, 16, 14, 0},
    {0x5101, 17, 15, 0}, {0x4801, 18, 16, 0}, {0x3801, 19, 17, 0}, {0x3401, 20, 18, 0},
    {0x3001, 21, 19, 0}, {0x2801, 22, 19, 0}, {0x2401, 23, 20, 0}, {0x2201, 24, 21, 0},
    {0x1C01, 25, 22, 0}, {0x1801, 26, 23, 0}, {0x1601, 27, 24, 0}, {0x1401, 28, 25, 0},
    {0x1201, 29, 26, 0}, {0x1101, 30, 27, 0}, {0x0AC1, 31, 28, 0}, {0x09C1, 32, 29, 0},
    {0x08A1, 33, 30, 0}, {0x0521, 34, 31, 0}, {0x0441, 35, 32, 0}, {0x02A1, 36, 33, 0},
    {0x0221, 37, 34, 0}, {0x0141, 38, 35, 0}, {0x0111, 39, 36, 0}, {0x0085, 40, 37, 0},
    {0x0049, 41, 38, 0}, {0x0025, 42, 39, 0}, {0x0015, 43, 40, 0}, {0x0009, 44, 41, 0},
    {0x0005, 45, 42, 0}, {0x0001, 45, 43, 0}, {0x5601, 46, 46, 0},
}};

}  // namespace

MqTestEncoder::MqTestEncoder() = default;

void MqTestEncoder::renormalise() {
  do {
    a_ <<= 1U;
    c_ <<= 1U;
    --count_;
    if (count_ == 0) {
      byte_out();
    }
  } while ((a_ & 0x8000U) == 0);
}

void MqTestEncoder::byte_out() {
  std::uint8_t& last = bytes_.back();
  if (last == 0xFF) {
    bytes_.push_back(static_cast<std::uint8_t>(c_ >> 20U));
    c_ &= 0xFFFFFU;
    count_ = 7;
    return;
  }
  if (c_ < 0x8000000U) {
    bytes_.push_back(static_cast<std::uint8_t>(c_ >> 19U));
    c_ &= 0x7FFFFU;
    count_ = 8;
    return;
  }
  ++last;
  if (last == 0xFF) {
    c_ &= 0x7FFFFFFU;
    bytes_.push_back(static_cast<std::uint8_t>(c_ >> 20U));
    c_ &= 0xFFFFFU;
    count_ = 7;
  } else {
    bytes_.push_back(static_cast<std::uint8_t>(c_ >> 19U));
    c_ &= 0x7FFFFU;
    count_ = 8;
  }
}

void MqTestEncoder::encode(std::uint8_t& context, unsigned bit) {
  unsigned index = context >> 1U;
  unsigned mps = context & 1U;
  const Qe& entry = k_table[index];
  a_ -= entry.qe;
  if (bit == mps) {
    // CODEMPS
    if ((a_ & 0x8000U) == 0) {
      if (a_ < entry.qe) {
        a_ = entry.qe;
      } else {
        c_ += entry.qe;
      }
      index = entry.nmps;
      renormalise();
    } else {
      c_ += entry.qe;
    }
  } else {
    // CODELPS
    if (a_ < entry.qe) {
      c_ += entry.qe;
    } else {
      a_ = entry.qe;
    }
    if (entry.sw == 1) {
      mps = 1U - mps;
    }
    index = entry.nlps;
    renormalise();
  }
  context = static_cast<std::uint8_t>((index << 1U) | mps);
}

void MqTestEncoder::encode_integer(IntegerContexts& contexts, std::optional<std::int64_t> value) {
  unsigned prev = 1;
  const auto put = [&](unsigned bit) {
    encode(contexts[prev], bit);
    prev = prev < 256 ? (prev << 1U) | bit : (((prev << 1U) | bit) & 511U) | 256U;
  };
  const auto put_bits = [&](std::uint64_t bits, unsigned count) {
    for (unsigned i = count; i > 0; --i) {
      put(static_cast<unsigned>((bits >> (i - 1U)) & 1U));
    }
  };
  if (!value) {
    put(1);          // sign: negative
    put(0);          // value below 4
    put_bits(0, 2);  // zero
    return;
  }
  const unsigned sign = *value < 0 ? 1U : 0U;
  const auto magnitude = static_cast<std::uint64_t>(*value < 0 ? -*value : *value);
  put(sign);
  if (magnitude < 4) {
    put(0);
    put_bits(magnitude, 2);
  } else if (magnitude < 20) {
    put(1);
    put(0);
    put_bits(magnitude - 4, 4);
  } else if (magnitude < 84) {
    put(1);
    put(1);
    put(0);
    put_bits(magnitude - 20, 6);
  } else if (magnitude < 340) {
    put(1);
    put(1);
    put(1);
    put(0);
    put_bits(magnitude - 84, 8);
  } else if (magnitude < 4436) {
    put(1);
    put(1);
    put(1);
    put(1);
    put(0);
    put_bits(magnitude - 340, 12);
  } else {
    put(1);
    put(1);
    put(1);
    put(1);
    put(1);
    put_bits(magnitude - 4436, 32);
  }
}

void MqTestEncoder::encode_iaid(std::vector<std::uint8_t>& contexts, unsigned code_length,
                                std::uint32_t value) {
  std::size_t prev = 1;
  for (unsigned i = code_length; i > 0; --i) {
    const unsigned bit = (value >> (i - 1U)) & 1U;
    encode(contexts.at(prev), bit);
    prev = (prev << 1U) | bit;
  }
}

std::vector<std::byte> MqTestEncoder::finish() {
  // SETBITS
  const std::uint32_t temp = c_ + a_;
  c_ |= 0xFFFFU;
  if (c_ >= temp) {
    c_ -= 0x8000U;
  }
  c_ <<= static_cast<unsigned>(count_);
  byte_out();
  c_ <<= static_cast<unsigned>(count_);
  byte_out();
  if (bytes_.back() == 0xFF) {
    bytes_.pop_back();
  }
  bytes_.push_back(0xFF);
  bytes_.push_back(0xAC);
  std::vector<std::byte> out;
  for (std::size_t i = 1; i < bytes_.size(); ++i) {
    out.push_back(static_cast<std::byte>(bytes_[i]));
  }
  return out;
}

}  // namespace pdfkit::test
