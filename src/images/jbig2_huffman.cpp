// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The Huffman coding of JBIG2: a port of the Huffman classes and functions of
// pdf.js src/core/jbig2.js at commit fe48736846e2043696738bd54435ebb5ab24ecff
// (read; Apache-2.0), over the annex B text of ITU-T T.88 (recalled): prefix
// codes assigned as annex B.3 says, the table of a Tables segment as annex B.2
// says, and the standard tables B.1 to B.15 as pdf.js lists them (each line is
// the range low value, the prefix length, the range length, the prefix code
// and its kind: 0 an ordinary or upper range line, 1 the lower range line, 2
// the out-of-band line).

#include "images/jbig2_huffman.hpp"

#include "images/decode_fault.hpp"
#include "platform/byte_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::images {

namespace {

struct StandardLine {
  std::int32_t low;
  std::uint8_t prefix_length;
  std::uint8_t range_length;
  std::uint16_t code;
  std::uint8_t kind;
};

// clang-format off
constexpr std::array<StandardLine, 4> k_table_b1 = {{
    {0, 1, 4, 0x0, 0}, {16, 2, 8, 0x2, 0}, {272, 3, 16, 0x6, 0}, {65808, 3, 32, 0x7, 0}
}};

constexpr std::array<StandardLine, 7> k_table_b2 = {{
    {0, 1, 0, 0x0, 0}, {1, 2, 0, 0x2, 0}, {2, 3, 0, 0x6, 0}, {3, 4, 3, 0xe, 0},
    {11, 5, 6, 0x1e, 0}, {75, 6, 32, 0x3e, 0}, {0, 6, 0, 0x3f, 2}
}};

constexpr std::array<StandardLine, 9> k_table_b3 = {{
    {-256, 8, 8, 0xfe, 0}, {0, 1, 0, 0x0, 0}, {1, 2, 0, 0x2, 0}, {2, 3, 0, 0x6, 0},
    {3, 4, 3, 0xe, 0}, {11, 5, 6, 0x1e, 0}, {-257, 8, 32, 0xff, 1}, {75, 7, 32, 0x7e, 0},
    {0, 6, 0, 0x3e, 2}
}};

constexpr std::array<StandardLine, 6> k_table_b4 = {{
    {1, 1, 0, 0x0, 0}, {2, 2, 0, 0x2, 0}, {3, 3, 0, 0x6, 0}, {4, 4, 3, 0xe, 0},
    {12, 5, 6, 0x1e, 0}, {76, 5, 32, 0x1f, 0}
}};

constexpr std::array<StandardLine, 8> k_table_b5 = {{
    {-255, 7, 8, 0x7e, 0}, {1, 1, 0, 0x0, 0}, {2, 2, 0, 0x2, 0}, {3, 3, 0, 0x6, 0},
    {4, 4, 3, 0xe, 0}, {12, 5, 6, 0x1e, 0}, {-256, 7, 32, 0x7f, 1}, {76, 6, 32, 0x3e, 0}
}};

constexpr std::array<StandardLine, 14> k_table_b6 = {{
    {-2048, 5, 10, 0x1c, 0}, {-1024, 4, 9, 0x8, 0}, {-512, 4, 8, 0x9, 0}, {-256, 4, 7, 0xa, 0},
    {-128, 5, 6, 0x1d, 0}, {-64, 5, 5, 0x1e, 0}, {-32, 4, 5, 0xb, 0}, {0, 2, 7, 0x0, 0},
    {128, 3, 7, 0x2, 0}, {256, 3, 8, 0x3, 0}, {512, 4, 9, 0xc, 0}, {1024, 4, 10, 0xd, 0},
    {-2049, 6, 32, 0x3e, 1}, {2048, 6, 32, 0x3f, 0}
}};

constexpr std::array<StandardLine, 15> k_table_b7 = {{
    {-1024, 4, 9, 0x8, 0}, {-512, 3, 8, 0x0, 0}, {-256, 4, 7, 0x9, 0}, {-128, 5, 6, 0x1a, 0},
    {-64, 5, 5, 0x1b, 0}, {-32, 4, 5, 0xa, 0}, {0, 4, 5, 0xb, 0}, {32, 5, 5, 0x1c, 0},
    {64, 5, 6, 0x1d, 0}, {128, 4, 7, 0xc, 0}, {256, 3, 8, 0x1, 0}, {512, 3, 9, 0x2, 0},
    {1024, 3, 10, 0x3, 0}, {-1025, 5, 32, 0x1e, 1}, {2048, 5, 32, 0x1f, 0}
}};

constexpr std::array<StandardLine, 21> k_table_b8 = {{
    {-15, 8, 3, 0xfc, 0}, {-7, 9, 1, 0x1fc, 0}, {-5, 8, 1, 0xfd, 0}, {-3, 9, 0, 0x1fd, 0},
    {-2, 7, 0, 0x7c, 0}, {-1, 4, 0, 0xa, 0}, {0, 2, 1, 0x0, 0}, {2, 5, 0, 0x1a, 0},
    {3, 6, 0, 0x3a, 0}, {4, 3, 4, 0x4, 0}, {20, 6, 1, 0x3b, 0}, {22, 4, 4, 0xb, 0},
    {38, 4, 5, 0xc, 0}, {70, 5, 6, 0x1b, 0}, {134, 5, 7, 0x1c, 0}, {262, 6, 7, 0x3c, 0},
    {390, 7, 8, 0x7d, 0}, {646, 6, 10, 0x3d, 0}, {-16, 9, 32, 0x1fe, 1},
    {1670, 9, 32, 0x1ff, 0}, {0, 2, 0, 0x1, 2}
}};

constexpr std::array<StandardLine, 22> k_table_b9 = {{
    {-31, 8, 4, 0xfc, 0}, {-15, 9, 2, 0x1fc, 0}, {-11, 8, 2, 0xfd, 0}, {-7, 9, 1, 0x1fd, 0},
    {-5, 7, 1, 0x7c, 0}, {-3, 4, 1, 0xa, 0}, {-1, 3, 1, 0x2, 0}, {1, 3, 1, 0x3, 0},
    {3, 5, 1, 0x1a, 0}, {5, 6, 1, 0x3a, 0}, {7, 3, 5, 0x4, 0}, {39, 6, 2, 0x3b, 0},
    {43, 4, 5, 0xb, 0}, {75, 4, 6, 0xc, 0}, {139, 5, 7, 0x1b, 0}, {267, 5, 8, 0x1c, 0},
    {523, 6, 8, 0x3c, 0}, {779, 7, 9, 0x7d, 0}, {1291, 6, 11, 0x3d, 0}, {-32, 9, 32, 0x1fe, 1},
    {3339, 9, 32, 0x1ff, 0}, {0, 2, 0, 0x0, 2}
}};

constexpr std::array<StandardLine, 21> k_table_b10 = {{
    {-21, 7, 4, 0x7a, 0}, {-5, 8, 0, 0xfc, 0}, {-4, 7, 0, 0x7b, 0}, {-3, 5, 0, 0x18, 0},
    {-2, 2, 2, 0x0, 0}, {2, 5, 0, 0x19, 0}, {3, 6, 0, 0x36, 0}, {4, 7, 0, 0x7c, 0},
    {5, 8, 0, 0xfd, 0}, {6, 2, 6, 0x1, 0}, {70, 5, 5, 0x1a, 0}, {102, 6, 5, 0x37, 0},
    {134, 6, 6, 0x38, 0}, {198, 6, 7, 0x39, 0}, {326, 6, 8, 0x3a, 0}, {582, 6, 9, 0x3b, 0},
    {1094, 6, 10, 0x3c, 0}, {2118, 7, 11, 0x7d, 0}, {-22, 8, 32, 0xfe, 1},
    {4166, 8, 32, 0xff, 0}, {0, 2, 0, 0x2, 2}
}};

constexpr std::array<StandardLine, 13> k_table_b11 = {{
    {1, 1, 0, 0x0, 0}, {2, 2, 1, 0x2, 0}, {4, 4, 0, 0xc, 0}, {5, 4, 1, 0xd, 0},
    {7, 5, 1, 0x1c, 0}, {9, 5, 2, 0x1d, 0}, {13, 6, 2, 0x3c, 0}, {17, 7, 2, 0x7a, 0},
    {21, 7, 3, 0x7b, 0}, {29, 7, 4, 0x7c, 0}, {45, 7, 5, 0x7d, 0}, {77, 7, 6, 0x7e, 0},
    {141, 7, 32, 0x7f, 0}
}};

constexpr std::array<StandardLine, 13> k_table_b12 = {{
    {1, 1, 0, 0x0, 0}, {2, 2, 0, 0x2, 0}, {3, 3, 1, 0x6, 0}, {5, 5, 0, 0x1c, 0},
    {6, 5, 1, 0x1d, 0}, {8, 6, 1, 0x3c, 0}, {10, 7, 0, 0x7a, 0}, {11, 7, 1, 0x7b, 0},
    {13, 7, 2, 0x7c, 0}, {17, 7, 3, 0x7d, 0}, {25, 7, 4, 0x7e, 0}, {41, 8, 5, 0xfe, 0},
    {73, 8, 32, 0xff, 0}
}};

constexpr std::array<StandardLine, 13> k_table_b13 = {{
    {1, 1, 0, 0x0, 0}, {2, 3, 0, 0x4, 0}, {3, 4, 0, 0xc, 0}, {4, 5, 0, 0x1c, 0},
    {5, 4, 1, 0xd, 0}, {7, 3, 3, 0x5, 0}, {15, 6, 1, 0x3a, 0}, {17, 6, 2, 0x3b, 0},
    {21, 6, 3, 0x3c, 0}, {29, 6, 4, 0x3d, 0}, {45, 6, 5, 0x3e, 0}, {77, 7, 6, 0x7e, 0},
    {141, 7, 32, 0x7f, 0}
}};

constexpr std::array<StandardLine, 5> k_table_b14 = {{
    {-2, 3, 0, 0x4, 0}, {-1, 3, 0, 0x5, 0}, {0, 1, 0, 0x0, 0}, {1, 3, 0, 0x6, 0},
    {2, 3, 0, 0x7, 0}
}};

constexpr std::array<StandardLine, 13> k_table_b15 = {{
    {-24, 7, 4, 0x7c, 0}, {-8, 6, 2, 0x3c, 0}, {-4, 5, 1, 0x1c, 0}, {-2, 4, 0, 0xc, 0},
    {-1, 3, 0, 0x4, 0}, {0, 1, 0, 0x0, 0}, {1, 3, 0, 0x5, 0}, {2, 4, 0, 0xd, 0},
    {3, 5, 1, 0x1d, 0}, {5, 6, 2, 0x3d, 0}, {9, 7, 4, 0x7d, 0}, {-25, 7, 32, 0x7e, 1},
    {25, 7, 32, 0x7f, 0}
}};
// clang-format on

constexpr std::uint8_t k_kind_lower = 1;
constexpr std::uint8_t k_kind_oob = 2;

Jbig2Huffman::Table build_standard(std::span<const StandardLine> lines) {
  std::vector<Jbig2Huffman::Line> out;
  out.reserve(lines.size());
  for (const StandardLine& line : lines) {
    Jbig2Huffman::Line entry;
    entry.prefix_length = line.prefix_length;
    entry.prefix_code = line.code;
    if (line.kind == k_kind_oob) {
      entry.is_oob = true;
    } else {
      entry.range_low = line.low;
      entry.range_length = line.range_length;
      entry.is_lower = line.kind == k_kind_lower;
    }
    out.push_back(entry);
  }
  return {std::move(out), true};
}

// Annex B.3: assigns the prefix codes of lines from their prefix lengths.
void assign_prefix_codes(std::vector<Jbig2Huffman::Line>& lines) {
  unsigned longest = 0;
  for (const Jbig2Huffman::Line& line : lines) {
    longest = std::max(longest, line.prefix_length);
  }
  std::vector<std::uint64_t> histogram(static_cast<std::size_t>(longest) + 1, 0);
  for (const Jbig2Huffman::Line& line : lines) {
    ++histogram[line.prefix_length];
  }
  histogram[0] = 0;
  std::uint64_t first_code = 0;
  for (unsigned length = 1; length <= longest; ++length) {
    first_code = (first_code + histogram[length - 1]) << 1U;
    std::uint64_t code = first_code;
    for (Jbig2Huffman::Line& line : lines) {
      if (line.prefix_length == length) {
        line.prefix_code = static_cast<std::uint32_t>(code);
        ++code;
      }
    }
  }
}

}  // namespace

std::uint32_t Jbig2Huffman::Reader::read_bit() {
  if (shift_ < 0) {
    if (position_ >= data_.size()) {
      throw DecodeFault("the JBIG2 Huffman-coded data ends inside a code");
    }
    current_ = std::to_integer<std::uint32_t>(data_[position_++]);
    shift_ = 7;
  }
  const std::uint32_t bit = (current_ >> static_cast<unsigned>(shift_)) & 1U;
  --shift_;
  return bit;
}

std::uint32_t Jbig2Huffman::Reader::read_bits(unsigned n) {
  std::uint32_t result = 0;
  for (unsigned i = 0; i < n; ++i) {
    result = (result << 1U) | read_bit();
  }
  return result;
}

Jbig2Huffman::Table::Table(std::vector<Line> lines, bool prefix_codes_done)
    : lines_(std::move(lines)) {
  if (!prefix_codes_done) {
    assign_prefix_codes(lines_);
  }
  nodes_.emplace_back();
  for (std::size_t index = 0; index < lines_.size(); ++index) {
    const Line& line = lines_[index];
    if (line.prefix_length == 0) {
      continue;
    }
    if (line.prefix_length > 32) {
      throw DecodeFault("a JBIG2 Huffman table holds a prefix longer than 32 bits");
    }
    std::uint32_t node = 0;
    for (unsigned shift = line.prefix_length; shift > 0; --shift) {
      const unsigned bit = (line.prefix_code >> (shift - 1U)) & 1U;
      if (nodes_[node].child[bit] == 0) {
        nodes_[node].child[bit] = static_cast<std::uint32_t>(nodes_.size());
        nodes_.emplace_back();
      }
      node = nodes_[node].child[bit];
    }
    nodes_[node].leaf = static_cast<std::uint32_t>(index) + 1U;
  }
}

std::optional<std::int64_t> Jbig2Huffman::Table::decode(Reader& reader) const {
  if (nodes_.empty()) {
    throw DecodeFault("a JBIG2 Huffman table has no codes");
  }
  std::uint32_t node = 0;
  while (nodes_[node].leaf == 0) {
    const std::uint32_t next = nodes_[node].child[reader.read_bit()];
    if (next == 0) {
      throw DecodeFault("the JBIG2 data holds a Huffman code its table does not define");
    }
    node = next;
  }
  const Line& line = lines_[nodes_[node].leaf - 1U];
  if (line.is_oob) {
    return std::nullopt;
  }
  const std::int64_t offset = reader.read_bits(line.range_length);
  return line.range_low + (line.is_lower ? -offset : offset);
}

const Jbig2Huffman::Table& Jbig2Huffman::standard_table(int number) {
  static const std::array<Table, 15> tables = {
      build_standard(k_table_b1),  build_standard(k_table_b2),  build_standard(k_table_b3),
      build_standard(k_table_b4),  build_standard(k_table_b5),  build_standard(k_table_b6),
      build_standard(k_table_b7),  build_standard(k_table_b8),  build_standard(k_table_b9),
      build_standard(k_table_b10), build_standard(k_table_b11), build_standard(k_table_b12),
      build_standard(k_table_b13), build_standard(k_table_b14), build_standard(k_table_b15),
  };
  if (number < 1 || number > 15) {
    throw DecodeFault("the JBIG2 standard Huffman table B." + std::to_string(number) +
                      " does not exist");
  }
  return tables[static_cast<std::size_t>(number - 1)];
}

Jbig2Huffman::Table Jbig2Huffman::read_tables_segment(platform::ByteSpan data) {
  // Annex B.2: flags, the lowest and highest values, then the lines.
  if (data.size() < 9) {
    throw DecodeFault("a JBIG2 Tables segment is shorter than its 9-byte header");
  }
  const auto flags = std::to_integer<unsigned>(data[0]);
  const auto lowest =
      static_cast<std::int32_t>(platform::read_be<std::uint32_t>(data, 1).value_or(0));
  const auto highest =
      static_cast<std::int32_t>(platform::read_be<std::uint32_t>(data, 5).value_or(0));
  Reader reader(data.subspan(9));
  const unsigned prefix_size_bits = ((flags >> 1U) & 7U) + 1U;
  const unsigned range_size_bits = ((flags >> 4U) & 7U) + 1U;
  std::vector<Line> lines;
  std::int64_t current_low = lowest;
  do {
    Line line;
    line.prefix_length = reader.read_bits(prefix_size_bits);
    line.range_length = reader.read_bits(range_size_bits);
    line.range_low = current_low;
    lines.push_back(line);
    current_low += static_cast<std::int64_t>(std::uint64_t{1} << std::min(line.range_length, 40U));
  } while (current_low < highest);

  Line lower;
  lower.prefix_length = reader.read_bits(prefix_size_bits);
  lower.range_low = static_cast<std::int64_t>(lowest) - 1;
  lower.range_length = 32;
  lower.is_lower = true;
  lines.push_back(lower);

  Line upper;
  upper.prefix_length = reader.read_bits(prefix_size_bits);
  upper.range_low = highest;
  upper.range_length = 32;
  lines.push_back(upper);

  if ((flags & 1U) != 0) {
    Line oob;
    oob.prefix_length = reader.read_bits(prefix_size_bits);
    oob.is_oob = true;
    lines.push_back(oob);
  }
  return {std::move(lines), false};
}

Jbig2Huffman::Table Jbig2Huffman::read_symbol_id_table(Reader& reader, std::uint64_t symbol_count) {
  // Run codes 0 to 34, each with a four-bit code length.
  std::vector<Line> run_codes;
  for (std::int64_t i = 0; i <= 34; ++i) {
    Line line;
    line.range_low = i;
    line.prefix_length = reader.read_bits(4);
    run_codes.push_back(line);
  }
  const Table run_code_table(std::move(run_codes), false);

  std::vector<Line> codes;
  while (codes.size() < symbol_count) {
    const std::int64_t code_length = run_code_table.decode(reader).value_or(0);
    if (code_length >= 32) {
      std::uint32_t repeats = 0;
      unsigned repeated_length = 0;
      if (code_length == 32) {
        if (codes.empty()) {
          throw DecodeFault("a JBIG2 symbol ID table repeats a code length before any exists");
        }
        repeats = reader.read_bits(2) + 3;
        repeated_length = codes.back().prefix_length;
      } else if (code_length == 33) {
        repeats = reader.read_bits(3) + 3;
      } else if (code_length == 34) {
        repeats = reader.read_bits(7) + 11;
      } else {
        throw DecodeFault("a JBIG2 symbol ID table holds a run code outside 0 to 34");
      }
      for (std::uint32_t j = 0; j < repeats && codes.size() < symbol_count; ++j) {
        Line line;
        line.range_low = static_cast<std::int64_t>(codes.size());
        line.prefix_length = repeated_length;
        codes.push_back(line);
      }
    } else {
      Line line;
      line.range_low = static_cast<std::int64_t>(codes.size());
      line.prefix_length = static_cast<unsigned>(code_length);
      codes.push_back(line);
    }
  }
  reader.byte_align();
  return {std::move(codes), false};
}

}  // namespace pdfkit::images
