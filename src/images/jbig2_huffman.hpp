// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Jbig2Huffman holds the Huffman coding of JBIG2 (ITU-T T.88 annex B): the bit
// reader the Huffman-coded procedures read from, the code tables built from
// lines of prefix length, range length and range low value (with the lower and
// upper range lines and the out-of-band line), the fifteen standard tables of
// annex B.5, the custom table of a Tables segment (annex B.2) and the symbol
// ID table of a Huffman text region (7.4.3.1.7). It is a port of the Huffman
// code of pdf.js src/core/jbig2.js at commit fe48736846e2043696738bd54435ebb5ab24ecff
// (Apache-2.0); the standard tables are pdf.js's, checked against annex B.5.

#ifndef PDFKIT_IMAGES_JBIG2_HUFFMAN_HPP
#define PDFKIT_IMAGES_JBIG2_HUFFMAN_HPP

#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pdfkit::images {

class Jbig2Huffman {
 public:
  // Reads bits most significant first from a byte range. A read past the end
  // throws DecodeFault.
  class Reader {
   public:
    explicit Reader(platform::ByteSpan data) : data_(data) {}

    [[nodiscard]] std::uint32_t read_bit();
    // n at most 32.
    [[nodiscard]] std::uint32_t read_bits(unsigned n);
    // Skips to the next byte boundary.
    void byte_align() noexcept { shift_ = -1; }
    // The index of the next byte not yet started.
    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    void set_position(std::size_t position) noexcept {
      position_ = position;
      shift_ = -1;
    }
    [[nodiscard]] platform::ByteSpan data() const noexcept { return data_; }

   private:
    platform::ByteSpan data_;
    std::size_t position_ = 0;
    int shift_ = -1;
    std::uint32_t current_ = 0;
  };

  // One line of a code table.
  struct Line {
    std::int64_t range_low = 0;
    unsigned prefix_length = 0;
    unsigned range_length = 0;
    std::uint32_t prefix_code = 0;
    bool is_oob = false;
    bool is_lower = false;
  };

  // A code table: decode reads one prefix and its range offset.
  class Table {
   public:
    Table() = default;
    // lines with prefix codes assigned when prefix_codes_done is false (annex
    // B.3). Throws DecodeFault on a prefix longer than 32 bits.
    Table(std::vector<Line> lines, bool prefix_codes_done);

    // The decoded value, or no value for the out-of-band code.
    [[nodiscard]] std::optional<std::int64_t> decode(Reader& reader) const;

   private:
    struct Node {
      // Index of the child for bit 0 and bit 1, or zero.
      std::uint32_t child[2] = {0, 0};
      // Index into lines_ plus one, or zero for an inner node.
      std::uint32_t leaf = 0;
    };
    std::vector<Line> lines_;
    std::vector<Node> nodes_;
  };

  // Standard table B.number, 1 to 15. Throws DecodeFault for another number.
  [[nodiscard]] static const Table& standard_table(int number);

  // The table a Tables segment (T.88 7.4.13, annex B.2) holds.
  [[nodiscard]] static Table read_tables_segment(platform::ByteSpan data);

  // The symbol ID table of a Huffman text region: 35 four-bit run code lengths,
  // then the code length of every symbol under those run codes, then a skip to
  // the next byte (7.4.3.1.7).
  [[nodiscard]] static Table read_symbol_id_table(Reader& reader, std::uint64_t symbol_count);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_JBIG2_HUFFMAN_HPP
