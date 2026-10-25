// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The test encoder for CCITT data: the make-up and terminating code tables of
// ITU-T T.4 (recalled, and cross-checked in the decoder tests against known
// codes such as white 0 = 00110101 and black 63 = 000001100111), and the
// two-dimensional mode codes of T.4 and T.6.

#include "images/ccitt_test_encoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::test {

namespace {

struct RunCode {
  int run;
  const char* code;
};

constexpr std::array<RunCode, 104> k_white_codes = {{{0, "00110101"},
                                                     {1, "000111"},
                                                     {2, "0111"},
                                                     {3, "1000"},
                                                     {4, "1011"},
                                                     {5, "1100"},
                                                     {6, "1110"},
                                                     {7, "1111"},
                                                     {8, "10011"},
                                                     {9, "10100"},
                                                     {10, "00111"},
                                                     {11, "01000"},
                                                     {12, "001000"},
                                                     {13, "000011"},
                                                     {14, "110100"},
                                                     {15, "110101"},
                                                     {16, "101010"},
                                                     {17, "101011"},
                                                     {18, "0100111"},
                                                     {19, "0001100"},
                                                     {20, "0001000"},
                                                     {21, "0010111"},
                                                     {22, "0000011"},
                                                     {23, "0000100"},
                                                     {24, "0101000"},
                                                     {25, "0101011"},
                                                     {26, "0010011"},
                                                     {27, "0100100"},
                                                     {28, "0011000"},
                                                     {29, "00000010"},
                                                     {30, "00000011"},
                                                     {31, "00011010"},
                                                     {32, "00011011"},
                                                     {33, "00010010"},
                                                     {34, "00010011"},
                                                     {35, "00010100"},
                                                     {36, "00010101"},
                                                     {37, "00010110"},
                                                     {38, "00010111"},
                                                     {39, "00101000"},
                                                     {40, "00101001"},
                                                     {41, "00101010"},
                                                     {42, "00101011"},
                                                     {43, "00101100"},
                                                     {44, "00101101"},
                                                     {45, "00000100"},
                                                     {46, "00000101"},
                                                     {47, "00001010"},
                                                     {48, "00001011"},
                                                     {49, "01010010"},
                                                     {50, "01010011"},
                                                     {51, "01010100"},
                                                     {52, "01010101"},
                                                     {53, "00100100"},
                                                     {54, "00100101"},
                                                     {55, "01011000"},
                                                     {56, "01011001"},
                                                     {57, "01011010"},
                                                     {58, "01011011"},
                                                     {59, "01001010"},
                                                     {60, "01001011"},
                                                     {61, "00110010"},
                                                     {62, "00110011"},
                                                     {63, "00110100"},
                                                     {64, "11011"},
                                                     {128, "10010"},
                                                     {192, "010111"},
                                                     {256, "0110111"},
                                                     {320, "00110110"},
                                                     {384, "00110111"},
                                                     {448, "01100100"},
                                                     {512, "01100101"},
                                                     {576, "01101000"},
                                                     {640, "01100111"},
                                                     {704, "011001100"},
                                                     {768, "011001101"},
                                                     {832, "011010010"},
                                                     {896, "011010011"},
                                                     {960, "011010100"},
                                                     {1024, "011010101"},
                                                     {1088, "011010110"},
                                                     {1152, "011010111"},
                                                     {1216, "011011000"},
                                                     {1280, "011011001"},
                                                     {1344, "011011010"},
                                                     {1408, "011011011"},
                                                     {1472, "010011000"},
                                                     {1536, "010011001"},
                                                     {1600, "010011010"},
                                                     {1664, "011000"},
                                                     {1728, "010011011"},
                                                     {1792, "00000001000"},
                                                     {1856, "00000001100"},
                                                     {1920, "00000001101"},
                                                     {1984, "000000010010"},
                                                     {2048, "000000010011"},
                                                     {2112, "000000010100"},
                                                     {2176, "000000010101"},
                                                     {2240, "000000010110"},
                                                     {2304, "000000010111"},
                                                     {2368, "000000011100"},
                                                     {2432, "000000011101"},
                                                     {2496, "000000011110"},
                                                     {2560, "000000011111"}}};
constexpr std::array<RunCode, 104> k_black_codes = {{{0, "0000110111"},
                                                     {1, "010"},
                                                     {2, "11"},
                                                     {3, "10"},
                                                     {4, "011"},
                                                     {5, "0011"},
                                                     {6, "0010"},
                                                     {7, "00011"},
                                                     {8, "000101"},
                                                     {9, "000100"},
                                                     {10, "0000100"},
                                                     {11, "0000101"},
                                                     {12, "0000111"},
                                                     {13, "00000100"},
                                                     {14, "00000111"},
                                                     {15, "000011000"},
                                                     {16, "0000010111"},
                                                     {17, "0000011000"},
                                                     {18, "0000001000"},
                                                     {19, "00001100111"},
                                                     {20, "00001101000"},
                                                     {21, "00001101100"},
                                                     {22, "00000110111"},
                                                     {23, "00000101000"},
                                                     {24, "00000010111"},
                                                     {25, "00000011000"},
                                                     {26, "000011001010"},
                                                     {27, "000011001011"},
                                                     {28, "000011001100"},
                                                     {29, "000011001101"},
                                                     {30, "000001101000"},
                                                     {31, "000001101001"},
                                                     {32, "000001101010"},
                                                     {33, "000001101011"},
                                                     {34, "000011010010"},
                                                     {35, "000011010011"},
                                                     {36, "000011010100"},
                                                     {37, "000011010101"},
                                                     {38, "000011010110"},
                                                     {39, "000011010111"},
                                                     {40, "000001101100"},
                                                     {41, "000001101101"},
                                                     {42, "000011011010"},
                                                     {43, "000011011011"},
                                                     {44, "000001010100"},
                                                     {45, "000001010101"},
                                                     {46, "000001010110"},
                                                     {47, "000001010111"},
                                                     {48, "000001100100"},
                                                     {49, "000001100101"},
                                                     {50, "000001010010"},
                                                     {51, "000001010011"},
                                                     {52, "000000100100"},
                                                     {53, "000000110111"},
                                                     {54, "000000111000"},
                                                     {55, "000000100111"},
                                                     {56, "000000101000"},
                                                     {57, "000001011000"},
                                                     {58, "000001011001"},
                                                     {59, "000000101011"},
                                                     {60, "000000101100"},
                                                     {61, "000001011010"},
                                                     {62, "000001100110"},
                                                     {63, "000001100111"},
                                                     {64, "0000001111"},
                                                     {128, "000011001000"},
                                                     {192, "000011001001"},
                                                     {256, "000001011011"},
                                                     {320, "000000110011"},
                                                     {384, "000000110100"},
                                                     {448, "000000110101"},
                                                     {512, "0000001101100"},
                                                     {576, "0000001101101"},
                                                     {640, "0000001001010"},
                                                     {704, "0000001001011"},
                                                     {768, "0000001001100"},
                                                     {832, "0000001001101"},
                                                     {896, "0000001110010"},
                                                     {960, "0000001110011"},
                                                     {1024, "0000001110100"},
                                                     {1088, "0000001110101"},
                                                     {1152, "0000001110110"},
                                                     {1216, "0000001110111"},
                                                     {1280, "0000001010010"},
                                                     {1344, "0000001010011"},
                                                     {1408, "0000001010100"},
                                                     {1472, "0000001010101"},
                                                     {1536, "0000001011010"},
                                                     {1600, "0000001011011"},
                                                     {1664, "0000001100100"},
                                                     {1728, "0000001100101"},
                                                     {1792, "00000001000"},
                                                     {1856, "00000001100"},
                                                     {1920, "00000001101"},
                                                     {1984, "000000010010"},
                                                     {2048, "000000010011"},
                                                     {2112, "000000010100"},
                                                     {2176, "000000010101"},
                                                     {2240, "000000010110"},
                                                     {2304, "000000010111"},
                                                     {2368, "000000011100"},
                                                     {2432, "000000011101"},
                                                     {2496, "000000011110"},
                                                     {2560, "000000011111"}}};

constexpr std::string_view k_eol = "000000000001";

[[nodiscard]] std::string lookup(const auto& table, std::size_t run) {
  for (const RunCode& entry : table) {
    if (std::cmp_equal(entry.run, run)) {
      return entry.code;
    }
  }
  return {};
}

std::string run_bits(bool black, std::size_t run) {
  const auto& table = black ? k_black_codes : k_white_codes;
  std::string bits;
  constexpr std::size_t longest_make_up = 2560;
  while (run >= longest_make_up) {
    bits += lookup(table, longest_make_up);
    run -= longest_make_up;
  }
  if (run >= 64) {
    bits += lookup(table, run / 64 * 64);
    run %= 64;
  }
  bits += lookup(table, run);
  return bits;
}

// A pixel of a row; a pixel left of the row is white.
bool pixel(const std::vector<bool>& row, long x) {
  return x >= 0 && static_cast<std::size_t>(x) < row.size() && row[static_cast<std::size_t>(x)];
}

// The first changing element right of a0 whose colour differs from colour and
// whose left neighbour has it, or the row width.
long find_b1(const std::vector<bool>& reference, long a0, bool colour) {
  const long width = static_cast<long>(reference.size());
  for (long x = a0 + 1; x < width; ++x) {
    if (pixel(reference, x) != colour && pixel(reference, x - 1) == colour) {
      return x;
    }
  }
  return width;
}

// The first pixel right of start of colour colour, or the row width.
long find_colour(const std::vector<bool>& row, long start, bool colour) {
  const long width = static_cast<long>(row.size());
  for (long x = start + 1; x < width; ++x) {
    if (pixel(row, x) == colour) {
      return x;
    }
  }
  return width;
}

std::string encode_two_dimensional(const std::vector<bool>& row,
                                   const std::vector<bool>& reference) {
  const long width = static_cast<long>(row.size());
  std::string bits;
  long a0 = -1;
  bool colour = false;
  while (a0 < width) {
    const long a1 = find_colour(row, a0, !colour);
    const long b1 = find_b1(reference, a0, colour);
    const long b2 = find_colour(reference, b1, colour);
    if (b2 < a1) {
      bits += "0001";
      a0 = b2;
    } else if (a1 - b1 >= -3 && a1 - b1 <= 3) {
      static constexpr std::array<const char*, 7> vertical = {"0000010", "000010", "010",    "1",
                                                              "011",     "000011", "0000011"};
      const long slot = a1 - b1 + 3;
      bits += vertical[static_cast<std::size_t>(slot)];
      a0 = a1;
      colour = !colour;
    } else {
      const long a2 = find_colour(row, a1, colour);
      const long start = std::max(a0, 0L);
      bits += "001";
      bits += run_bits(colour, static_cast<std::size_t>(a1 - start));
      bits += run_bits(!colour, static_cast<std::size_t>(a2 - a1));
      a0 = a2;
    }
  }
  return bits;
}

std::string encode_one_dimensional(const std::vector<bool>& row) {
  std::string bits;
  bool colour = false;
  std::size_t x = 0;
  while (x < row.size()) {
    std::size_t end = x;
    while (end < row.size() && row[end] == colour) {
      ++end;
    }
    bits += run_bits(colour, end - x);
    x = end;
    colour = !colour;
  }
  return bits;
}

void align_to_byte(std::string& bits) {
  while (bits.size() % 8 != 0) {
    bits.push_back('0');
  }
}

}  // namespace

std::string CcittTestEncoder::run_code(bool black, std::size_t run) {
  return run_bits(black, run);
}

std::vector<std::byte> CcittTestEncoder::pack(std::string_view bits) {
  std::vector<std::byte> out((bits.size() + 7) / 8, std::byte{0});
  for (std::size_t i = 0; i < bits.size(); ++i) {
    if (bits[i] == '1') {
      out[i / 8] |= static_cast<std::byte>(0x80U >> (i % 8));
    }
  }
  return out;
}

std::vector<std::byte> CcittTestEncoder::encode(const BitonalRows& rows, const Options& options) {
  std::string bits;
  const std::size_t width = rows.empty() ? 0 : rows.front().size();
  const std::vector<bool> white(width, false);
  const bool group4 = options.k < 0;
  const bool two_dimensional_group3 = options.k > 0;
  const bool end_of_line = options.end_of_line || two_dimensional_group3;
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const std::vector<bool>& row = rows[index];
    if (group4) {
      if (options.byte_align) {
        align_to_byte(bits);
      }
      bits += encode_two_dimensional(row, index == 0 ? white : rows[index - 1]);
      continue;
    }
    if (end_of_line) {
      if (options.byte_align) {
        if (options.eol_ends_on_boundary) {
          while ((bits.size() + k_eol.size()) % 8 != 0) {
            bits.push_back('0');
          }
        } else {
          // The fill bits go before the line, which starts with its end-of-line code.
          align_to_byte(bits);
        }
      }
      bits += k_eol;
    } else if (options.byte_align) {
      align_to_byte(bits);
    }
    if (two_dimensional_group3) {
      const bool one_dimensional = index % static_cast<std::size_t>(options.k) == 0;
      bits += one_dimensional ? '1' : '0';
      bits += one_dimensional ? encode_one_dimensional(row)
                              : encode_two_dimensional(row, rows[index - 1]);
    } else {
      bits += encode_one_dimensional(row);
    }
  }
  if (options.end_of_block) {
    if (group4) {
      bits += k_eol;
      bits += k_eol;
    } else {
      for (int i = 0; i < 6; ++i) {
        bits += k_eol;
        if (two_dimensional_group3) {
          bits += '1';
        }
      }
    }
  }
  return pack(bits);
}

}  // namespace pdfkit::test
