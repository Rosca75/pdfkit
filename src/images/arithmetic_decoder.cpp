// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The MQ arithmetic decoder: a port of pdf.js src/core/arithmetic_decoder.js at
// commit fe48736846e2043696738bd54435ebb5ab24ecff (read; Apache-2.0). The
// probability-estimation table is Table C-2 of the JPEG 2000 standard
// (ITU-T T.800), which is Table E.1 of the JBIG2 standard (ITU-T T.88),
// unchanged from pdf.js's copy.

#include "images/arithmetic_decoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pdfkit::images {

namespace {

struct QeEntry {
  std::uint16_t qe;
  std::uint8_t next_more_probable;
  std::uint8_t next_less_probable;
  std::uint8_t switch_flag;
};

constexpr std::array<QeEntry, 47> k_qe_table = {{
    {0x5601, 1, 1, 1},   {0x3401, 2, 6, 0},   {0x1801, 3, 9, 0},   {0x0ac1, 4, 12, 0},
    {0x0521, 5, 29, 0},  {0x0221, 38, 33, 0}, {0x5601, 7, 6, 1},   {0x5401, 8, 14, 0},
    {0x4801, 9, 14, 0},  {0x3801, 10, 14, 0}, {0x3001, 11, 17, 0}, {0x2401, 12, 18, 0},
    {0x1c01, 13, 20, 0}, {0x1601, 29, 21, 0}, {0x5601, 15, 14, 1}, {0x5401, 16, 14, 0},
    {0x5101, 17, 15, 0}, {0x4801, 18, 16, 0}, {0x3801, 19, 17, 0}, {0x3401, 20, 18, 0},
    {0x3001, 21, 19, 0}, {0x2801, 22, 19, 0}, {0x2401, 23, 20, 0}, {0x2201, 24, 21, 0},
    {0x1c01, 25, 22, 0}, {0x1801, 26, 23, 0}, {0x1601, 27, 24, 0}, {0x1401, 28, 25, 0},
    {0x1201, 29, 26, 0}, {0x1101, 30, 27, 0}, {0x0ac1, 31, 28, 0}, {0x09c1, 32, 29, 0},
    {0x08a1, 33, 30, 0}, {0x0521, 34, 31, 0}, {0x0441, 35, 32, 0}, {0x02a1, 36, 33, 0},
    {0x0221, 37, 34, 0}, {0x0141, 38, 35, 0}, {0x0111, 39, 36, 0}, {0x0085, 40, 37, 0},
    {0x0049, 41, 38, 0}, {0x0025, 42, 39, 0}, {0x0015, 43, 40, 0}, {0x0009, 44, 41, 0},
    {0x0005, 45, 42, 0}, {0x0001, 45, 43, 0}, {0x5601, 46, 46, 0},
}};

}  // namespace

ArithmeticDecoder::ArithmeticDecoder(platform::ByteSpan data) : data_(data) {
  // C.3.5 Initialisation of the decoder (INITDEC)
  chigh_ = byte_at(0);
  clow_ = 0;
  byte_in();
  chigh_ = ((chigh_ << 7U) & 0xffffU) | ((clow_ >> 9U) & 0x7fU);
  clow_ = (clow_ << 7U) & 0xffffU;
  count_ -= 7;
  a_ = 0x8000;
}

std::uint32_t ArithmeticDecoder::byte_at(std::size_t index) const noexcept {
  return index < data_.size() ? std::to_integer<std::uint32_t>(data_[index]) : 0xffU;
}

// C.3.4 Compressed data input (BYTEIN)
void ArithmeticDecoder::byte_in() {
  if (byte_at(position_) == 0xffU) {
    if (byte_at(position_ + 1) > 0x8fU) {
      clow_ += 0xff00U;
      count_ = 8;
    } else {
      ++position_;
      clow_ += byte_at(position_) << 9U;
      count_ = 7;
    }
  } else {
    ++position_;
    clow_ += position_ < data_.size() ? byte_at(position_) << 8U : 0xff00U;
    count_ = 8;
  }
  if (clow_ > 0xffffU) {
    chigh_ += clow_ >> 16U;
    clow_ &= 0xffffU;
  }
}

// C.3.2 Decoding a decision (DECODE)
std::uint32_t ArithmeticDecoder::read_bit(std::uint8_t& context) {
  std::uint32_t index = context >> 1U;
  std::uint32_t more_probable = context & 1U;
  const QeEntry& entry = k_qe_table[index];
  const std::uint32_t qe = entry.qe;
  std::uint32_t decision = 0;
  std::uint32_t a = a_ - qe;

  if (chigh_ < qe) {
    // exchangeLps
    if (a < qe) {
      a = qe;
      decision = more_probable;
      index = entry.next_more_probable;
    } else {
      a = qe;
      decision = 1U ^ more_probable;
      if (entry.switch_flag == 1) {
        more_probable = decision;
      }
      index = entry.next_less_probable;
    }
  } else {
    chigh_ -= qe;
    if ((a & 0x8000U) != 0) {
      a_ = a;
      return more_probable;
    }
    // exchangeMps
    if (a < qe) {
      decision = 1U ^ more_probable;
      if (entry.switch_flag == 1) {
        more_probable = decision;
      }
      index = entry.next_less_probable;
    } else {
      decision = more_probable;
      index = entry.next_more_probable;
    }
  }
  // C.3.3 renormD
  do {
    if (count_ == 0) {
      byte_in();
    }
    a <<= 1U;
    chigh_ = ((chigh_ << 1U) & 0xffffU) | ((clow_ >> 15U) & 1U);
    clow_ = (clow_ << 1U) & 0xffffU;
    --count_;
  } while ((a & 0x8000U) == 0);
  a_ = a;
  context = static_cast<std::uint8_t>((index << 1U) | more_probable);
  return decision;
}

}  // namespace pdfkit::images
