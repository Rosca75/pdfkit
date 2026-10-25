// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The CCITT decoder: a port of pdf.js src/core/ccitt.js at commit
// fe48736846e2043696738bd54435ebb5ab24ecff (read; Apache-2.0), which is itself
// a JavaScript port of xpdf's CCITTFaxStream (Glyph & Cog, LLC, Apache-2.0
// terms). pdf.js decodes byte by byte through a state machine; this port reads
// one row at a time into the run boundaries of a coding line and renders the
// row into packed bytes, which yields the same bytes. The mode and run-length
// tables below are pdf.js's tables unchanged (index: the next bits of the
// stream; entry: code length and value), so a code that pdf.js decodes decodes
// here to the same value.
//
// Where this port differs from pdf.js, each difference makes a fault visible or
// keeps memory safe, and none changes the pixels of a well-formed stream:
// - the data ending inside a row, an invalid two-dimensional code and an
//   absent row (the end-of-block code, a return-to-control code or the end of
//   the data where a row would start) are told apart, where pdf.js reports each
//   as the end of the data and pads the row with white;
// - a truncated row ends in white, where pdf.js reads on, alternating black and
//   white pixels, over the end of the data;
// - every index into the run tables and the coding and reference lines is
//   bounds-checked, where pdf.js reads and writes typed arrays past their end
//   as no-ops;
// - /DamagedRowsBeforeError is honoured by reading a damaged row as white and
//   counting it, where pdf.js ignores the parameter.

#include "images/ccitt_decoder.hpp"

#include "platform/checked.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::images {

namespace {

constexpr int k_eol = -2;
constexpr int k_eof = -1;
// What look_bits answers when no bit is left.
constexpr std::uint32_t k_no_bits = 0xFFFFFFFFU;
constexpr int k_pass = 0;
constexpr int k_horizontal = 1;
constexpr int k_vertical_0 = 2;
constexpr int k_vertical_r1 = 3;
constexpr int k_vertical_l1 = 4;
constexpr int k_vertical_r2 = 5;
constexpr int k_vertical_l2 = 6;
constexpr int k_vertical_r3 = 7;
constexpr int k_vertical_l3 = 8;

// A table entry: the length in bits of the code the index starts with (-1 when
// no code does) and the code's value (a run length, a mode, or k_eol).
struct Entry {
  std::int16_t bits;
  std::int16_t value;
};

// clang-format off
constexpr std::array<Entry, 128> k_two_dim_table = {{
  {-1, -1}, {-1, -1}, // 000000x
  {7, k_vertical_l3}, // 0000010
  {7, k_vertical_r3}, // 0000011
  {6, k_vertical_l2}, {6, k_vertical_l2}, // 000010x
  {6, k_vertical_r2}, {6, k_vertical_r2}, // 000011x
  {4, k_pass}, {4, k_pass}, // 0001xxx
  {4, k_pass}, {4, k_pass},
  {4, k_pass}, {4, k_pass},
  {4, k_pass}, {4, k_pass},
  {3, k_horizontal}, {3, k_horizontal}, // 001xxxx
  {3, k_horizontal}, {3, k_horizontal},
  {3, k_horizontal}, {3, k_horizontal},
  {3, k_horizontal}, {3, k_horizontal},
  {3, k_horizontal}, {3, k_horizontal},
  {3, k_horizontal}, {3, k_horizontal},
  {3, k_horizontal}, {3, k_horizontal},
  {3, k_horizontal}, {3, k_horizontal},
  {3, k_vertical_l1}, {3, k_vertical_l1}, // 010xxxx
  {3, k_vertical_l1}, {3, k_vertical_l1},
  {3, k_vertical_l1}, {3, k_vertical_l1},
  {3, k_vertical_l1}, {3, k_vertical_l1},
  {3, k_vertical_l1}, {3, k_vertical_l1},
  {3, k_vertical_l1}, {3, k_vertical_l1},
  {3, k_vertical_l1}, {3, k_vertical_l1},
  {3, k_vertical_l1}, {3, k_vertical_l1},
  {3, k_vertical_r1}, {3, k_vertical_r1}, // 011xxxx
  {3, k_vertical_r1}, {3, k_vertical_r1},
  {3, k_vertical_r1}, {3, k_vertical_r1},
  {3, k_vertical_r1}, {3, k_vertical_r1},
  {3, k_vertical_r1}, {3, k_vertical_r1},
  {3, k_vertical_r1}, {3, k_vertical_r1},
  {3, k_vertical_r1}, {3, k_vertical_r1},
  {3, k_vertical_r1}, {3, k_vertical_r1},
  {1, k_vertical_0}, {1, k_vertical_0}, // 1xxxxxx
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0},
  {1, k_vertical_0}, {1, k_vertical_0}
}};

constexpr std::array<Entry, 32> k_white_table_1 = {{
  {-1, -1}, // 00000
  {12, k_eol}, // 00001
  {-1, -1}, {-1, -1}, // 0001x
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 001xx
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 010xx
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 011xx
  {11, 1792}, {11, 1792}, // 1000x
  {12, 1984}, // 10010
  {12, 2048}, // 10011
  {12, 2112}, // 10100
  {12, 2176}, // 10101
  {12, 2240}, // 10110
  {12, 2304}, // 10111
  {11, 1856}, {11, 1856}, // 1100x
  {11, 1920}, {11, 1920}, // 1101x
  {12, 2368}, // 11100
  {12, 2432}, // 11101
  {12, 2496}, // 11110
  {12, 2560} // 11111
}};

constexpr std::array<Entry, 512> k_white_table_2 = {{
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 0000000xx
  {8, 29}, {8, 29}, // 00000010x
  {8, 30}, {8, 30}, // 00000011x
  {8, 45}, {8, 45}, // 00000100x
  {8, 46}, {8, 46}, // 00000101x
  {7, 22}, {7, 22}, {7, 22}, {7, 22}, // 0000011xx
  {7, 23}, {7, 23}, {7, 23}, {7, 23}, // 0000100xx
  {8, 47}, {8, 47}, // 00001010x
  {8, 48}, {8, 48}, // 00001011x
  {6, 13}, {6, 13}, {6, 13}, {6, 13}, // 000011xxx
  {6, 13}, {6, 13}, {6, 13}, {6, 13},
  {7, 20}, {7, 20}, {7, 20}, {7, 20}, // 0001000xx
  {8, 33}, {8, 33}, // 00010010x
  {8, 34}, {8, 34}, // 00010011x
  {8, 35}, {8, 35}, // 00010100x
  {8, 36}, {8, 36}, // 00010101x
  {8, 37}, {8, 37}, // 00010110x
  {8, 38}, {8, 38}, // 00010111x
  {7, 19}, {7, 19}, {7, 19}, {7, 19}, // 0001100xx
  {8, 31}, {8, 31}, // 00011010x
  {8, 32}, {8, 32}, // 00011011x
  {6, 1}, {6, 1}, {6, 1}, {6, 1}, // 000111xxx
  {6, 1}, {6, 1}, {6, 1}, {6, 1},
  {6, 12}, {6, 12}, {6, 12}, {6, 12}, // 001000xxx
  {6, 12}, {6, 12}, {6, 12}, {6, 12},
  {8, 53}, {8, 53}, // 00100100x
  {8, 54}, {8, 54}, // 00100101x
  {7, 26}, {7, 26}, {7, 26}, {7, 26}, // 0010011xx
  {8, 39}, {8, 39}, // 00101000x
  {8, 40}, {8, 40}, // 00101001x
  {8, 41}, {8, 41}, // 00101010x
  {8, 42}, {8, 42}, // 00101011x
  {8, 43}, {8, 43}, // 00101100x
  {8, 44}, {8, 44}, // 00101101x
  {7, 21}, {7, 21}, {7, 21}, {7, 21}, // 0010111xx
  {7, 28}, {7, 28}, {7, 28}, {7, 28}, // 0011000xx
  {8, 61}, {8, 61}, // 00110010x
  {8, 62}, {8, 62}, // 00110011x
  {8, 63}, {8, 63}, // 00110100x
  {8, 0}, {8, 0}, // 00110101x
  {8, 320}, {8, 320}, // 00110110x
  {8, 384}, {8, 384}, // 00110111x
  {5, 10}, {5, 10}, {5, 10}, {5, 10}, // 00111xxxx
  {5, 10}, {5, 10}, {5, 10}, {5, 10},
  {5, 10}, {5, 10}, {5, 10}, {5, 10},
  {5, 10}, {5, 10}, {5, 10}, {5, 10},
  {5, 11}, {5, 11}, {5, 11}, {5, 11}, // 01000xxxx
  {5, 11}, {5, 11}, {5, 11}, {5, 11},
  {5, 11}, {5, 11}, {5, 11}, {5, 11},
  {5, 11}, {5, 11}, {5, 11}, {5, 11},
  {7, 27}, {7, 27}, {7, 27}, {7, 27}, // 0100100xx
  {8, 59}, {8, 59}, // 01001010x
  {8, 60}, {8, 60}, // 01001011x
  {9, 1472}, // 010011000
  {9, 1536}, // 010011001
  {9, 1600}, // 010011010
  {9, 1728}, // 010011011
  {7, 18}, {7, 18}, {7, 18}, {7, 18}, // 0100111xx
  {7, 24}, {7, 24}, {7, 24}, {7, 24}, // 0101000xx
  {8, 49}, {8, 49}, // 01010010x
  {8, 50}, {8, 50}, // 01010011x
  {8, 51}, {8, 51}, // 01010100x
  {8, 52}, {8, 52}, // 01010101x
  {7, 25}, {7, 25}, {7, 25}, {7, 25}, // 0101011xx
  {8, 55}, {8, 55}, // 01011000x
  {8, 56}, {8, 56}, // 01011001x
  {8, 57}, {8, 57}, // 01011010x
  {8, 58}, {8, 58}, // 01011011x
  {6, 192}, {6, 192}, {6, 192}, {6, 192}, // 010111xxx
  {6, 192}, {6, 192}, {6, 192}, {6, 192},
  {6, 1664}, {6, 1664}, {6, 1664}, {6, 1664}, // 011000xxx
  {6, 1664}, {6, 1664}, {6, 1664}, {6, 1664},
  {8, 448}, {8, 448}, // 01100100x
  {8, 512}, {8, 512}, // 01100101x
  {9, 704}, // 011001100
  {9, 768}, // 011001101
  {8, 640}, {8, 640}, // 01100111x
  {8, 576}, {8, 576}, // 01101000x
  {9, 832}, // 011010010
  {9, 896}, // 011010011
  {9, 960}, // 011010100
  {9, 1024}, // 011010101
  {9, 1088}, // 011010110
  {9, 1152}, // 011010111
  {9, 1216}, // 011011000
  {9, 1280}, // 011011001
  {9, 1344}, // 011011010
  {9, 1408}, // 011011011
  {7, 256}, {7, 256}, {7, 256}, {7, 256}, // 0110111xx
  {4, 2}, {4, 2}, {4, 2}, {4, 2}, // 0111xxxxx
  {4, 2}, {4, 2}, {4, 2}, {4, 2},
  {4, 2}, {4, 2}, {4, 2}, {4, 2},
  {4, 2}, {4, 2}, {4, 2}, {4, 2},
  {4, 2}, {4, 2}, {4, 2}, {4, 2},
  {4, 2}, {4, 2}, {4, 2}, {4, 2},
  {4, 2}, {4, 2}, {4, 2}, {4, 2},
  {4, 2}, {4, 2}, {4, 2}, {4, 2},
  {4, 3}, {4, 3}, {4, 3}, {4, 3}, // 1000xxxxx
  {4, 3}, {4, 3}, {4, 3}, {4, 3},
  {4, 3}, {4, 3}, {4, 3}, {4, 3},
  {4, 3}, {4, 3}, {4, 3}, {4, 3},
  {4, 3}, {4, 3}, {4, 3}, {4, 3},
  {4, 3}, {4, 3}, {4, 3}, {4, 3},
  {4, 3}, {4, 3}, {4, 3}, {4, 3},
  {4, 3}, {4, 3}, {4, 3}, {4, 3},
  {5, 128}, {5, 128}, {5, 128}, {5, 128}, // 10010xxxx
  {5, 128}, {5, 128}, {5, 128}, {5, 128},
  {5, 128}, {5, 128}, {5, 128}, {5, 128},
  {5, 128}, {5, 128}, {5, 128}, {5, 128},
  {5, 8}, {5, 8}, {5, 8}, {5, 8}, // 10011xxxx
  {5, 8}, {5, 8}, {5, 8}, {5, 8},
  {5, 8}, {5, 8}, {5, 8}, {5, 8},
  {5, 8}, {5, 8}, {5, 8}, {5, 8},
  {5, 9}, {5, 9}, {5, 9}, {5, 9}, // 10100xxxx
  {5, 9}, {5, 9}, {5, 9}, {5, 9},
  {5, 9}, {5, 9}, {5, 9}, {5, 9},
  {5, 9}, {5, 9}, {5, 9}, {5, 9},
  {6, 16}, {6, 16}, {6, 16}, {6, 16}, // 101010xxx
  {6, 16}, {6, 16}, {6, 16}, {6, 16},
  {6, 17}, {6, 17}, {6, 17}, {6, 17}, // 101011xxx
  {6, 17}, {6, 17}, {6, 17}, {6, 17},
  {4, 4}, {4, 4}, {4, 4}, {4, 4}, // 1011xxxxx
  {4, 4}, {4, 4}, {4, 4}, {4, 4},
  {4, 4}, {4, 4}, {4, 4}, {4, 4},
  {4, 4}, {4, 4}, {4, 4}, {4, 4},
  {4, 4}, {4, 4}, {4, 4}, {4, 4},
  {4, 4}, {4, 4}, {4, 4}, {4, 4},
  {4, 4}, {4, 4}, {4, 4}, {4, 4},
  {4, 4}, {4, 4}, {4, 4}, {4, 4},
  {4, 5}, {4, 5}, {4, 5}, {4, 5}, // 1100xxxxx
  {4, 5}, {4, 5}, {4, 5}, {4, 5},
  {4, 5}, {4, 5}, {4, 5}, {4, 5},
  {4, 5}, {4, 5}, {4, 5}, {4, 5},
  {4, 5}, {4, 5}, {4, 5}, {4, 5},
  {4, 5}, {4, 5}, {4, 5}, {4, 5},
  {4, 5}, {4, 5}, {4, 5}, {4, 5},
  {4, 5}, {4, 5}, {4, 5}, {4, 5},
  {6, 14}, {6, 14}, {6, 14}, {6, 14}, // 110100xxx
  {6, 14}, {6, 14}, {6, 14}, {6, 14},
  {6, 15}, {6, 15}, {6, 15}, {6, 15}, // 110101xxx
  {6, 15}, {6, 15}, {6, 15}, {6, 15},
  {5, 64}, {5, 64}, {5, 64}, {5, 64}, // 11011xxxx
  {5, 64}, {5, 64}, {5, 64}, {5, 64},
  {5, 64}, {5, 64}, {5, 64}, {5, 64},
  {5, 64}, {5, 64}, {5, 64}, {5, 64},
  {4, 6}, {4, 6}, {4, 6}, {4, 6}, // 1110xxxxx
  {4, 6}, {4, 6}, {4, 6}, {4, 6},
  {4, 6}, {4, 6}, {4, 6}, {4, 6},
  {4, 6}, {4, 6}, {4, 6}, {4, 6},
  {4, 6}, {4, 6}, {4, 6}, {4, 6},
  {4, 6}, {4, 6}, {4, 6}, {4, 6},
  {4, 6}, {4, 6}, {4, 6}, {4, 6},
  {4, 6}, {4, 6}, {4, 6}, {4, 6},
  {4, 7}, {4, 7}, {4, 7}, {4, 7}, // 1111xxxxx
  {4, 7}, {4, 7}, {4, 7}, {4, 7},
  {4, 7}, {4, 7}, {4, 7}, {4, 7},
  {4, 7}, {4, 7}, {4, 7}, {4, 7},
  {4, 7}, {4, 7}, {4, 7}, {4, 7},
  {4, 7}, {4, 7}, {4, 7}, {4, 7},
  {4, 7}, {4, 7}, {4, 7}, {4, 7},
  {4, 7}, {4, 7}, {4, 7}, {4, 7}
}};

constexpr std::array<Entry, 128> k_black_table_1 = {{
  {-1, -1}, {-1, -1}, // 000000000000x
  {12, k_eol}, {12, k_eol}, // 000000000001x
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 00000000001xx
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 00000000010xx
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 00000000011xx
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 00000000100xx
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 00000000101xx
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 00000000110xx
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 00000000111xx
  {11, 1792}, {11, 1792}, {11, 1792}, {11, 1792}, // 00000001000xx
  {12, 1984}, {12, 1984}, // 000000010010x
  {12, 2048}, {12, 2048}, // 000000010011x
  {12, 2112}, {12, 2112}, // 000000010100x
  {12, 2176}, {12, 2176}, // 000000010101x
  {12, 2240}, {12, 2240}, // 000000010110x
  {12, 2304}, {12, 2304}, // 000000010111x
  {11, 1856}, {11, 1856}, {11, 1856}, {11, 1856}, // 00000001100xx
  {11, 1920}, {11, 1920}, {11, 1920}, {11, 1920}, // 00000001101xx
  {12, 2368}, {12, 2368}, // 000000011100x
  {12, 2432}, {12, 2432}, // 000000011101x
  {12, 2496}, {12, 2496}, // 000000011110x
  {12, 2560}, {12, 2560}, // 000000011111x
  {10, 18}, {10, 18}, {10, 18}, {10, 18}, // 0000001000xxx
  {10, 18}, {10, 18}, {10, 18}, {10, 18},
  {12, 52}, {12, 52}, // 000000100100x
  {13, 640}, // 0000001001010
  {13, 704}, // 0000001001011
  {13, 768}, // 0000001001100
  {13, 832}, // 0000001001101
  {12, 55}, {12, 55}, // 000000100111x
  {12, 56}, {12, 56}, // 000000101000x
  {13, 1280}, // 0000001010010
  {13, 1344}, // 0000001010011
  {13, 1408}, // 0000001010100
  {13, 1472}, // 0000001010101
  {12, 59}, {12, 59}, // 000000101011x
  {12, 60}, {12, 60}, // 000000101100x
  {13, 1536}, // 0000001011010
  {13, 1600}, // 0000001011011
  {11, 24}, {11, 24}, {11, 24}, {11, 24}, // 00000010111xx
  {11, 25}, {11, 25}, {11, 25}, {11, 25}, // 00000011000xx
  {13, 1664}, // 0000001100100
  {13, 1728}, // 0000001100101
  {12, 320}, {12, 320}, // 000000110011x
  {12, 384}, {12, 384}, // 000000110100x
  {12, 448}, {12, 448}, // 000000110101x
  {13, 512}, // 0000001101100
  {13, 576}, // 0000001101101
  {12, 53}, {12, 53}, // 000000110111x
  {12, 54}, {12, 54}, // 000000111000x
  {13, 896}, // 0000001110010
  {13, 960}, // 0000001110011
  {13, 1024}, // 0000001110100
  {13, 1088}, // 0000001110101
  {13, 1152}, // 0000001110110
  {13, 1216}, // 0000001110111
  {10, 64}, {10, 64}, {10, 64}, {10, 64}, // 0000001111xxx
  {10, 64}, {10, 64}, {10, 64}, {10, 64}
}};

constexpr std::array<Entry, 192> k_black_table_2 = {{
  {8, 13}, {8, 13}, {8, 13}, {8, 13}, // 00000100xxxx
  {8, 13}, {8, 13}, {8, 13}, {8, 13},
  {8, 13}, {8, 13}, {8, 13}, {8, 13},
  {8, 13}, {8, 13}, {8, 13}, {8, 13},
  {11, 23}, {11, 23}, // 00000101000x
  {12, 50}, // 000001010010
  {12, 51}, // 000001010011
  {12, 44}, // 000001010100
  {12, 45}, // 000001010101
  {12, 46}, // 000001010110
  {12, 47}, // 000001010111
  {12, 57}, // 000001011000
  {12, 58}, // 000001011001
  {12, 61}, // 000001011010
  {12, 256}, // 000001011011
  {10, 16}, {10, 16}, {10, 16}, {10, 16}, // 0000010111xx
  {10, 17}, {10, 17}, {10, 17}, {10, 17}, // 0000011000xx
  {12, 48}, // 000001100100
  {12, 49}, // 000001100101
  {12, 62}, // 000001100110
  {12, 63}, // 000001100111
  {12, 30}, // 000001101000
  {12, 31}, // 000001101001
  {12, 32}, // 000001101010
  {12, 33}, // 000001101011
  {12, 40}, // 000001101100
  {12, 41}, // 000001101101
  {11, 22}, {11, 22}, // 00000110111x
  {8, 14}, {8, 14}, {8, 14}, {8, 14}, // 00000111xxxx
  {8, 14}, {8, 14}, {8, 14}, {8, 14},
  {8, 14}, {8, 14}, {8, 14}, {8, 14},
  {8, 14}, {8, 14}, {8, 14}, {8, 14},
  {7, 10}, {7, 10}, {7, 10}, {7, 10}, // 0000100xxxxx
  {7, 10}, {7, 10}, {7, 10}, {7, 10},
  {7, 10}, {7, 10}, {7, 10}, {7, 10},
  {7, 10}, {7, 10}, {7, 10}, {7, 10},
  {7, 10}, {7, 10}, {7, 10}, {7, 10},
  {7, 10}, {7, 10}, {7, 10}, {7, 10},
  {7, 10}, {7, 10}, {7, 10}, {7, 10},
  {7, 10}, {7, 10}, {7, 10}, {7, 10},
  {7, 11}, {7, 11}, {7, 11}, {7, 11}, // 0000101xxxxx
  {7, 11}, {7, 11}, {7, 11}, {7, 11},
  {7, 11}, {7, 11}, {7, 11}, {7, 11},
  {7, 11}, {7, 11}, {7, 11}, {7, 11},
  {7, 11}, {7, 11}, {7, 11}, {7, 11},
  {7, 11}, {7, 11}, {7, 11}, {7, 11},
  {7, 11}, {7, 11}, {7, 11}, {7, 11},
  {7, 11}, {7, 11}, {7, 11}, {7, 11},
  {9, 15}, {9, 15}, {9, 15}, {9, 15}, // 000011000xxx
  {9, 15}, {9, 15}, {9, 15}, {9, 15},
  {12, 128}, // 000011001000
  {12, 192}, // 000011001001
  {12, 26}, // 000011001010
  {12, 27}, // 000011001011
  {12, 28}, // 000011001100
  {12, 29}, // 000011001101
  {11, 19}, {11, 19}, // 00001100111x
  {11, 20}, {11, 20}, // 00001101000x
  {12, 34}, // 000011010010
  {12, 35}, // 000011010011
  {12, 36}, // 000011010100
  {12, 37}, // 000011010101
  {12, 38}, // 000011010110
  {12, 39}, // 000011010111
  {11, 21}, {11, 21}, // 00001101100x
  {12, 42}, // 000011011010
  {12, 43}, // 000011011011
  {10, 0}, {10, 0}, {10, 0}, {10, 0}, // 0000110111xx
  {7, 12}, {7, 12}, {7, 12}, {7, 12}, // 0000111xxxxx
  {7, 12}, {7, 12}, {7, 12}, {7, 12},
  {7, 12}, {7, 12}, {7, 12}, {7, 12},
  {7, 12}, {7, 12}, {7, 12}, {7, 12},
  {7, 12}, {7, 12}, {7, 12}, {7, 12},
  {7, 12}, {7, 12}, {7, 12}, {7, 12},
  {7, 12}, {7, 12}, {7, 12}, {7, 12},
  {7, 12}, {7, 12}, {7, 12}, {7, 12}
}};

constexpr std::array<Entry, 64> k_black_table_3 = {{
  {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, // 0000xx
  {6, 9}, // 000100
  {6, 8}, // 000101
  {5, 7}, {5, 7}, // 00011x
  {4, 6}, {4, 6}, {4, 6}, {4, 6}, // 0010xx
  {4, 5}, {4, 5}, {4, 5}, {4, 5}, // 0011xx
  {3, 1}, {3, 1}, {3, 1}, {3, 1}, // 010xxx
  {3, 1}, {3, 1}, {3, 1}, {3, 1},
  {3, 4}, {3, 4}, {3, 4}, {3, 4}, // 011xxx
  {3, 4}, {3, 4}, {3, 4}, {3, 4},
  {2, 3}, {2, 3}, {2, 3}, {2, 3}, // 10xxxx
  {2, 3}, {2, 3}, {2, 3}, {2, 3},
  {2, 3}, {2, 3}, {2, 3}, {2, 3},
  {2, 3}, {2, 3}, {2, 3}, {2, 3},
  {2, 2}, {2, 2}, {2, 2}, {2, 2}, // 11xxxx
  {2, 2}, {2, 2}, {2, 2}, {2, 2},
  {2, 2}, {2, 2}, {2, 2}, {2, 2},
  {2, 2}, {2, 2}, {2, 2}, {2, 2}
}};
// clang-format on

// A code of the run-length tables never exceeds 13 bits, and a row never has
// more run boundaries than columns plus the sentinels the arrays reserve.
constexpr std::size_t k_line_slack = 5;

class FaxReader {
 public:
  // Why a row stopped before its last pixel.
  enum class Stop : std::uint8_t {
    none,
    // The row would have started with the end-of-block code, a return-to-control
    // code or no data at all: no row exists.
    end_of_data,
    // The data ended inside the row.
    dry,
    // A two-dimensional code matched no mode.
    invalid,
  };

  FaxReader(platform::ByteSpan data, const CcittDecoder::Parameters& parameters)
      : data_(data),
        encoding_(parameters.k),
        eoline_(parameters.end_of_line),
        byte_align_(parameters.encoded_byte_align),
        eoblock_(parameters.end_of_block),
        rows_(parameters.rows),
        columns_(parameters.columns),
        coding_line_(static_cast<std::size_t>(parameters.columns) + k_line_slack, 0),
        ref_line_(static_cast<std::size_t>(parameters.columns) + k_line_slack, 0),
        next_line_2d_(parameters.k < 0) {
    coding_line_[0] = static_cast<std::uint32_t>(columns_);
    std::uint32_t code1 = 0;
    while ((code1 = look_bits(12)) == 0) {
      eat_bits(1);
    }
    if (code1 == 1) {
      eat_bits(12);
    }
    if (encoding_ > 0) {
      next_line_2d_ = look_bits(1) == 0;
      eat_bits(1);
    }
  }

  // Reads the next row into the coding line. False when no row follows.
  bool read_row() {
    if (rows_done_) {
      eof_ = true;
    }
    if (eof_ || only_padding_left()) {
      eof_ = true;
      return false;
    }
    err_ = false;
    bad_code_ = false;
    dry_ = false;
    stop_ = Stop::none;
    codes_in_row_ = 0;
    if (next_line_2d_) {
      read_two_dimensional();
    } else {
      read_one_dimensional();
    }
    if (stop_ == Stop::end_of_data) {
      eof_ = true;
      return false;
    }
    if (stop_ != Stop::none) {
      add_pixels(columns_, false);
      eof_ = true;
      return true;
    }
    if (!finish_row()) {
      return false;
    }
    ++row_;
    return true;
  }

  // Whether the row just read was damaged: a wrong length, a code that matched
  // nothing, or a stop inside the row.
  [[nodiscard]] bool damaged() const noexcept { return err_ || bad_code_ || stop_ != Stop::none; }
  [[nodiscard]] Stop stop() const noexcept { return stop_; }
  [[nodiscard]] std::int64_t row() const noexcept { return row_; }

  // Consumes the end-of-facsimile-block code of a Group 4 stream that follows
  // the last row, less the end-of-line code the end of that row already took.
  void consume_end_of_block() {
    for (int remaining = last_row_took_eol_ ? 1 : 2; remaining > 0; --remaining) {
      if (look_bits(12) != 1) {
        break;
      }
      eat_bits(12);
    }
  }

  // The bytes read so far, counting a byte of which only some bits were used.
  [[nodiscard]] std::size_t bytes_consumed() const noexcept {
    const std::size_t bits = position_ * 8U - input_bits_;
    return (bits + 7U) / 8U;
  }

  // Renders the coding line into out, which holds (columns + 7) / 8 bytes:
  // white pixels are 1 bits, black pixels 0 bits, and the pad bits of the last
  // byte are 0 (all inverted under /BlackIs1, as pdf.js does).
  void render(std::span<std::uint8_t> out, bool black_is_1, bool all_white) const {
    std::ranges::fill(out, std::uint8_t{0});
    const auto set_ones = [&](std::int64_t from, std::int64_t to) {
      for (std::int64_t x = from; x < to;) {
        const auto index = static_cast<std::size_t>(x / 8);
        const auto bit = static_cast<unsigned>(x % 8);
        const std::int64_t span_end = std::min<std::int64_t>(to, x - x % 8 + 8);
        const auto count = static_cast<unsigned>(span_end - x);
        const unsigned mask = ((1U << count) - 1U) << (8U - bit - count);
        out[index] = static_cast<std::uint8_t>(out[index] | mask);
        x = span_end;
      }
    };
    if (all_white) {
      set_ones(0, columns_);
    } else {
      std::int64_t start = 0;
      for (std::size_t i = 0; i <= coding_pos_ && i < coding_line_.size(); ++i) {
        const std::int64_t end = std::min<std::int64_t>(coding_line_[i], columns_);
        if (end > start) {
          if ((i & 1U) == 0) {
            set_ones(start, end);
          }
          start = end;
        }
        if (start >= columns_) {
          break;
        }
      }
    }
    if (black_is_1) {
      for (std::uint8_t& byte : out) {
        byte = static_cast<std::uint8_t>(byte ^ 0xFFU);
      }
    }
  }

 private:
  struct Found {
    bool found;
    int value;
    bool matched;
  };

  [[nodiscard]] std::int64_t coding_at(std::size_t index) const noexcept {
    return index < coding_line_.size() ? static_cast<std::int64_t>(coding_line_[index]) : columns_;
  }

  [[nodiscard]] std::int64_t ref_at(std::size_t index) const noexcept {
    return index < ref_line_.size() ? static_cast<std::int64_t>(ref_line_[index]) : columns_;
  }

  // The next n bits (n at most 13), zero-padded when the data ends inside
  // them, or k_no_bits when no bit is left.
  std::uint32_t look_bits(unsigned n) {
    while (input_bits_ < n) {
      if (position_ >= data_.size()) {
        if (input_bits_ == 0) {
          return k_no_bits;
        }
        return (input_buf_ << (n - input_bits_)) & (0xFFFFU >> (16U - n));
      }
      input_buf_ = (input_buf_ << 8U) | std::to_integer<std::uint32_t>(data_[position_++]);
      input_bits_ += 8;
    }
    return (input_buf_ >> (input_bits_ - n)) & (0xFFFFU >> (16U - n));
  }

  // Whether nothing but zero bits is left: the padding that follows the last
  // row, which no code starts with.
  [[nodiscard]] bool only_padding_left() const noexcept {
    if (position_ < data_.size()) {
      return false;
    }
    const std::uint32_t mask = (std::uint32_t{1} << input_bits_) - 1U;
    return (input_buf_ & mask) == 0;
  }

  void eat_bits(unsigned n) { input_bits_ = input_bits_ > n ? input_bits_ - n : 0; }

  void add_pixels(std::int64_t a1, bool black_pixels) {
    std::size_t pos = coding_pos_;
    if (a1 > coding_at(pos)) {
      if (a1 > columns_) {
        err_ = true;
        a1 = columns_;
      }
      if (((pos & 1U) != 0) != black_pixels) {
        ++pos;
      }
      if (pos >= coding_line_.size()) {
        // More boundaries than a row can hold: end the row here.
        err_ = true;
        pos = coding_line_.size() - 1;
        a1 = columns_;
      }
      coding_line_[pos] = static_cast<std::uint32_t>(a1);
    }
    coding_pos_ = pos;
  }

  void add_pixels_neg(std::int64_t a1, bool black_pixels) {
    std::size_t pos = coding_pos_;
    if (a1 > coding_at(pos)) {
      add_pixels(a1, black_pixels);
      return;
    }
    if (a1 < coding_at(pos)) {
      if (a1 < 0) {
        err_ = true;
        a1 = 0;
      }
      while (pos > 0 && a1 < coding_at(pos - 1)) {
        --pos;
      }
      if (pos < coding_line_.size()) {
        coding_line_[pos] = static_cast<std::uint32_t>(a1);
      }
    }
    coding_pos_ = pos;
  }

  // Finds a code of length start to end in table, reading the index from the
  // next i bits shifted up to end bits; limit shifts the index down.
  Found find_table_code(unsigned start, unsigned end, std::span<const Entry> table,
                        std::uint32_t limit) {
    for (unsigned i = start; i <= end; ++i) {
      std::uint32_t code = look_bits(i);
      if (code == k_no_bits) {
        return {true, 1, false};
      }
      if (i < end) {
        code <<= end - i;
      }
      if (limit == 0 || code >= limit) {
        const std::size_t index = code - limit;
        if (index < table.size() && std::cmp_equal(table[index].bits, i)) {
          eat_bits(i);
          return {true, table[index].value, true};
        }
      }
    }
    return {false, 0, false};
  }

  int get_two_dim_code() {
    if (eoblock_) {
      const std::uint32_t code = look_bits(7);
      if (code == k_no_bits) {
        dry_ = true;
        return k_eof;
      }
      const Entry& entry = k_two_dim_table[code];
      if (entry.bits > 0) {
        eat_bits(static_cast<unsigned>(entry.bits));
        return entry.value;
      }
    } else {
      const Found result = find_table_code(1, 7, k_two_dim_table, 0);
      if (result.found && result.matched) {
        return result.value;
      }
      if (result.found) {
        dry_ = true;
        return k_eof;
      }
    }
    bad_code_ = true;
    return k_eof;
  }

  int get_white_code() {
    if (eoblock_) {
      const std::uint32_t code = look_bits(12);
      if (code == k_no_bits) {
        dry_ = true;
        return 1;
      }
      const Entry& entry = (code >> 5U) == 0 ? k_white_table_1[code] : k_white_table_2[code >> 3U];
      if (entry.bits > 0) {
        eat_bits(static_cast<unsigned>(entry.bits));
        return entry.value;
      }
    } else {
      Found result = find_table_code(1, 9, k_white_table_2, 0);
      if (!result.found) {
        result = find_table_code(11, 12, k_white_table_1, 0);
      }
      if (result.found) {
        dry_ = dry_ || !result.matched;
        return result.value;
      }
    }
    bad_code_ = true;
    eat_bits(1);
    return 1;
  }

  int get_black_code() {
    if (eoblock_) {
      const std::uint32_t code = look_bits(13);
      if (code == k_no_bits) {
        dry_ = true;
        return 1;
      }
      Entry entry{};
      if ((code >> 7U) == 0) {
        entry = k_black_table_1[code];
      } else if ((code >> 9U) == 0) {
        entry = k_black_table_2[(code >> 1U) - 64U];
      } else {
        entry = k_black_table_3[code >> 7U];
      }
      if (entry.bits > 0) {
        eat_bits(static_cast<unsigned>(entry.bits));
        return entry.value;
      }
    } else {
      Found result = find_table_code(2, 6, k_black_table_3, 0);
      if (!result.found) {
        result = find_table_code(7, 12, k_black_table_2, 64);
      }
      if (!result.found) {
        result = find_table_code(10, 13, k_black_table_1, 0);
      }
      if (result.found) {
        dry_ = dry_ || !result.matched;
        return result.value;
      }
    }
    bad_code_ = true;
    eat_bits(1);
    return 1;
  }

  // One run of the colour, made of its make-up codes and its terminating code.
  std::int64_t read_run(bool black) {
    std::int64_t total = 0;
    int code = 0;
    do {
      code = black ? get_black_code() : get_white_code();
      total += code;
      if (dry_) {
        break;
      }
    } while (code >= 64);
    return total;
  }

  void read_one_dimensional() {
    coding_line_[0] = 0;
    coding_pos_ = 0;
    bool black_pixels = false;
    while (coding_at(coding_pos_) < columns_) {
      const std::int64_t run = read_run(black_pixels);
      if (dry_) {
        stop_ = codes_in_row_ == 0 ? Stop::end_of_data : Stop::dry;
        return;
      }
      if (codes_in_row_ == 0 && run == k_eol) {
        stop_ = Stop::end_of_data;
        return;
      }
      ++codes_in_row_;
      add_pixels(coding_at(coding_pos_) + run, black_pixels);
      black_pixels = !black_pixels;
    }
  }

  // After a vertical mode that moved a0 to a1: the reference changing element
  // b1 is the first to the right of a0 of the opposite colour.
  void advance_reference(std::size_t& ref_pos, bool left) {
    if (coding_at(coding_pos_) < columns_) {
      if (left && ref_pos > 0) {
        --ref_pos;
      } else {
        ++ref_pos;
      }
      skip_reference(ref_pos);
    }
  }

  void skip_reference(std::size_t& ref_pos) const {
    while (ref_at(ref_pos) <= coding_at(coding_pos_) && ref_at(ref_pos) < columns_) {
      ref_pos += 2;
    }
  }

  void read_two_dimensional() {
    std::size_t i = 0;
    while (i < coding_line_.size() && coding_at(i) < columns_ && i + 2 < ref_line_.size()) {
      ref_line_[i] = coding_line_[i];
      ++i;
    }
    ref_line_[i++] = static_cast<std::uint32_t>(columns_);
    ref_line_[i] = static_cast<std::uint32_t>(columns_);
    coding_line_[0] = 0;
    coding_pos_ = 0;
    std::size_t ref_pos = 0;
    bool black_pixels = false;
    while (coding_at(coding_pos_) < columns_) {
      const int mode = get_two_dim_code();
      if (mode == k_eof) {
        stop_ = codes_in_row_ == 0 ? Stop::end_of_data : (dry_ ? Stop::dry : Stop::invalid);
        return;
      }
      ++codes_in_row_;
      switch (mode) {
        case k_pass:
          add_pixels(ref_at(ref_pos + 1), black_pixels);
          if (ref_at(ref_pos + 1) < columns_) {
            ref_pos += 2;
          }
          break;
        case k_horizontal: {
          std::int64_t run1 = 0;
          std::int64_t run2 = 0;
          if (black_pixels) {
            run1 = read_run(true);
            run2 = read_run(false);
          } else {
            run1 = read_run(false);
            run2 = read_run(true);
          }
          if (dry_) {
            stop_ = Stop::dry;
            return;
          }
          add_pixels(coding_at(coding_pos_) + run1, black_pixels);
          if (coding_at(coding_pos_) < columns_) {
            add_pixels(coding_at(coding_pos_) + run2, !black_pixels);
          }
          skip_reference(ref_pos);
          break;
        }
        case k_vertical_r3:
        case k_vertical_r2:
        case k_vertical_r1:
        case k_vertical_0: {
          const int offset = mode == k_vertical_0
                                 ? 0
                                 : (mode == k_vertical_r1 ? 1 : (mode == k_vertical_r2 ? 2 : 3));
          add_pixels(ref_at(ref_pos) + offset, black_pixels);
          black_pixels = !black_pixels;
          advance_reference(ref_pos, false);
          break;
        }
        case k_vertical_l3:
        case k_vertical_l2:
        case k_vertical_l1: {
          const int offset = mode == k_vertical_l1 ? 1 : (mode == k_vertical_l2 ? 2 : 3);
          add_pixels_neg(ref_at(ref_pos) - offset, black_pixels);
          black_pixels = !black_pixels;
          advance_reference(ref_pos, true);
          break;
        }
        default:
          bad_code_ = true;
          add_pixels(columns_, false);
          err_ = true;
          break;
      }
    }
  }

  // The end of a row: byte alignment, the end-of-line code and the one- or
  // two-dimensional tag bit of the next row, and the return-to-control code.
  // False when the data ended where a row's end-of-line code is searched for.
  bool finish_row() {
    bool got_eol = false;
    std::uint32_t code1 = 0;
    if (byte_align_) {
      input_bits_ -= input_bits_ % 8U;
    }
    if (!eoblock_ && row_ == rows_ - 1) {
      rows_done_ = true;
    } else {
      code1 = look_bits(12);
      if (eoline_) {
        while (code1 != k_no_bits && code1 != 1) {
          eat_bits(1);
          code1 = look_bits(12);
        }
      } else {
        while (code1 == 0) {
          eat_bits(1);
          code1 = look_bits(12);
        }
      }
      if (code1 == 1) {
        eat_bits(12);
        got_eol = true;
      } else if (code1 == k_no_bits) {
        eof_ = true;
      }
    }
    last_row_took_eol_ = got_eol;
    if (!eof_ && encoding_ > 0 && !rows_done_) {
      next_line_2d_ = look_bits(1) == 0;
      eat_bits(1);
    }
    if (eoblock_ && got_eol && byte_align_) {
      code1 = look_bits(12);
      if (code1 == 1) {
        eat_bits(12);
        if (encoding_ > 0) {
          eat_bits(1);
        }
        if (encoding_ >= 0) {
          for (int i = 0; i < 4; ++i) {
            eat_bits(12);
            if (encoding_ > 0) {
              eat_bits(1);
            }
          }
        }
        eof_ = true;
      }
    } else if (err_ && eoline_) {
      while (true) {
        code1 = look_bits(13);
        if (code1 == k_no_bits) {
          eof_ = true;
          return false;
        }
        if ((code1 >> 1U) == 1) {
          break;
        }
        eat_bits(1);
      }
      eat_bits(12);
      if (encoding_ > 0) {
        eat_bits(1);
        next_line_2d_ = (code1 & 1U) == 0;
      }
    }
    return true;
  }

  platform::ByteSpan data_;
  std::size_t position_ = 0;
  std::uint32_t input_buf_ = 0;
  unsigned input_bits_ = 0;
  std::int64_t encoding_;
  bool eoline_;
  bool byte_align_;
  bool eoblock_;
  std::int64_t rows_;
  std::int64_t columns_;
  std::vector<std::uint32_t> coding_line_;
  std::vector<std::uint32_t> ref_line_;
  std::size_t coding_pos_ = 0;
  std::int64_t row_ = 0;
  bool next_line_2d_;
  bool eof_ = false;
  bool rows_done_ = false;
  bool err_ = false;
  bool bad_code_ = false;
  bool dry_ = false;
  bool last_row_took_eol_ = false;
  Stop stop_ = Stop::none;
  int codes_in_row_ = 0;
};

}  // namespace

CcittDecoder::Result CcittDecoder::decode(platform::ByteSpan data, const Parameters& parameters,
                                          platform::Budget& budget, std::string_view object) {
  Result result;
  if (parameters.columns < 1) {
    result.fault = "the /Columns of the CCITT data is " + std::to_string(parameters.columns) +
                   ", which is not a positive width";
    return result;
  }
  const auto columns = static_cast<std::uint64_t>(parameters.columns);
  const std::uint64_t row_bytes = (columns + 7U) / 8U;
  // Two lines of 32-bit run boundaries, and one packed row.
  const std::string claimant = std::string("the /Columns of ") + std::string(object);
  budget.check_claim(platform::checked_mul<std::uint64_t>(columns, 8U, claimant), claimant);
  if (columns > std::numeric_limits<std::uint32_t>::max() / 4U) {
    result.fault = "the /Columns of the CCITT data is " + std::to_string(columns) +
                   ", wider than the decoder reads";
    return result;
  }
  result.columns = static_cast<std::uint32_t>(columns);

  FaxReader reader(data, parameters);
  std::vector<std::uint8_t> row(static_cast<std::size_t>(row_bytes));
  std::uint64_t stream_used = 0;
  const std::int64_t damaged_limit =
      std::max<std::int64_t>(parameters.damaged_rows_before_error, 0);
  std::string fault;
  while (parameters.rows <= 0 || std::cmp_less(result.rows, parameters.rows)) {
    if (!reader.read_row()) {
      break;
    }
    budget.charge(row_bytes, stream_used, object);
    const bool damaged = reader.damaged();
    if (damaged) {
      ++result.damaged_rows;
    }
    reader.render(row, parameters.black_is_1, damaged && damaged_limit > 0);
    for (const std::uint8_t byte : row) {
      result.data.push_back(static_cast<std::byte>(byte));
    }
    ++result.rows;
    if (reader.stop() == FaxReader::Stop::dry) {
      fault = "the CCITT data ends inside row " + std::to_string(result.rows);
    } else if (reader.stop() == FaxReader::Stop::invalid) {
      fault = "the CCITT data holds a code that is no two-dimensional mode in row " +
              std::to_string(result.rows);
    }
  }
  if (fault.empty() && parameters.rows > 0 && std::cmp_less(result.rows, parameters.rows)) {
    fault = "the CCITT data holds " + std::to_string(result.rows) + " of the " +
            std::to_string(parameters.rows) + " rows the /Rows entry states";
  }
  result.fault = std::move(fault);
  if (parameters.k < 0 && parameters.end_of_block) {
    reader.consume_end_of_block();
  }
  result.bytes_consumed = reader.bytes_consumed();
  return result;
}

}  // namespace pdfkit::images
