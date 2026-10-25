// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The segment structure of JBIG2: segment headers (T.88 7.2) and the parameter
// fields of each segment type (T.88 7.4), read as pdf.js reads them in
// src/core/jbig2.js at commit fe48736846e2043696738bd54435ebb5ab24ecff (read;
// Apache-2.0). Differences from pdf.js: every field read is bounds-checked and
// a truncation is a DecodeFault naming the segment, a segment whose stated
// length runs past the data is clamped to the data, an unknown segment length
// is resolved relative to the data's start (pdf.js adds the absolute index),
// and a stream that opens with the file header is read in either organisation.

#include "images/jbig2_segments.hpp"

#include "images/decode_fault.hpp"
#include "platform/byte_span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::images {

namespace {

constexpr std::array<std::uint8_t, 8> k_file_header_id = {0x97, 0x4a, 0x42, 0x32,
                                                          0x0d, 0x0a, 0x1a, 0x0a};
constexpr std::uint32_t k_unknown_length = 0xFFFFFFFFU;
constexpr std::size_t k_region_info_size = 17;

// Reads fields in order, throwing DecodeFault when the data ends first.
class Cursor {
 public:
  Cursor(platform::ByteSpan data, std::string what, std::size_t position = 0)
      : data_(data), what_(std::move(what)), position_(position) {}

  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return position_ <= data_.size() ? data_.size() - position_ : 0;
  }

  std::uint32_t u8() { return take<std::uint8_t>(); }
  std::uint32_t u16() { return take<std::uint16_t>(); }
  std::uint32_t u32() { return take<std::uint32_t>(); }
  int i8() { return static_cast<std::int8_t>(take<std::uint8_t>()); }
  std::int32_t i32() { return static_cast<std::int32_t>(take<std::uint32_t>()); }
  void skip(std::size_t count) {
    if (remaining() < count) {
      throw DecodeFault(what_ + " is truncated");
    }
    position_ += count;
  }

 private:
  template <typename T>
  std::uint32_t take() {
    const auto value = platform::read_be<T>(data_, position_);
    if (!value) {
      throw DecodeFault(what_ + " is truncated");
    }
    position_ += sizeof(T);
    return *value;
  }

  platform::ByteSpan data_;
  std::string what_;
  std::size_t position_;
};

std::string describe(const Jbig2Segments::Header& header) {
  return "the JBIG2 segment " + std::to_string(header.number) + " (" +
         Jbig2Segments::type_name(header.type) + ")";
}

Jbig2Segments::RegionInfo read_region_info(Cursor& cursor) {
  Jbig2Segments::RegionInfo info;
  info.width = cursor.u32();
  info.height = cursor.u32();
  info.x = cursor.u32();
  info.y = cursor.u32();
  info.combination_operator = cursor.u8() & 7U;
  return info;
}

std::vector<Jbig2Segments::AdaptivePixel> read_adaptive_pixels(Cursor& cursor, unsigned count) {
  std::vector<Jbig2Segments::AdaptivePixel> pixels;
  for (unsigned i = 0; i < count; ++i) {
    Jbig2Segments::AdaptivePixel pixel;
    pixel.x = cursor.i8();
    pixel.y = cursor.i8();
    pixels.push_back(pixel);
  }
  return pixels;
}

platform::ByteSpan rest(const Jbig2Segments::Header& header, const Cursor& cursor) {
  return header.data.subspan(std::min(cursor.position(), header.data.size()));
}

struct ParsedHeader {
  Jbig2Segments::Header header;
  std::uint64_t length = 0;
  std::size_t end = 0;
};

// 7.2: one segment header starting at position.
ParsedHeader parse_header(platform::ByteSpan data, std::size_t position) {
  Cursor cursor(data, "a JBIG2 segment header", position);
  ParsedHeader parsed;
  Jbig2Segments::Header& header = parsed.header;
  header.number = cursor.u32();
  const std::uint32_t flags = cursor.u8();
  header.type = flags & 0x3fU;
  const std::string describe_segment = "the JBIG2 segment " + std::to_string(header.number);
  if (std::string_view(Jbig2Segments::type_name(header.type)) == "unknown") {
    throw DecodeFault(describe_segment + " has the unknown type " + std::to_string(header.type));
  }
  const bool long_page_association = (flags & 0x40U) != 0;

  // 7.2.4: the referred-to segment count and the retention flags.
  const std::uint32_t referred_flags = cursor.u8();
  std::uint64_t referred_count = (referred_flags >> 5U) & 7U;
  if (referred_count == 7) {
    // Long form: a 29-bit count and ceil((count + 1) / 8) retention bytes. The
    // flags byte just read is the top of the four-byte field.
    Cursor long_form(data, "a JBIG2 segment header", cursor.position() - 1);
    referred_count = long_form.u32() & 0x1fffffffU;
    cursor.skip(3);
    cursor.skip(static_cast<std::size_t>((referred_count + 8) >> 3U));
  } else if (referred_count == 5 || referred_count == 6) {
    throw DecodeFault(describe_segment + " has invalid referred-to flags");
  }

  // 7.2.5: the numbers of the referred-to segments, sized by this segment's own
  // number.
  std::size_t number_size = 4;
  if (header.number <= 256) {
    number_size = 1;
  } else if (header.number <= 65536) {
    number_size = 2;
  }
  if (referred_count > cursor.remaining() / number_size) {
    throw DecodeFault(describe_segment + " refers to more segments than its header holds");
  }
  for (std::uint64_t i = 0; i < referred_count; ++i) {
    header.referred_to.push_back(
        number_size == 1 ? cursor.u8() : (number_size == 2 ? cursor.u16() : cursor.u32()));
  }
  header.page_association = long_page_association ? cursor.u32() : cursor.u8();
  const std::uint32_t length = cursor.u32();
  parsed.end = cursor.position();
  parsed.length = length;

  if (length == k_unknown_length) {
    // 7.2.7: only an immediate generic region may have an unknown length. Its
    // data ends with the arithmetic coder's 0xFF 0xAC marker (or two zero bytes
    // for MMR) followed by the four-byte row count, which is searched for.
    if (header.type != Jbig2Segments::immediate_generic_region &&
        header.type != Jbig2Segments::immediate_lossless_generic_region) {
      throw DecodeFault(describe_segment + " has an unknown length but is no generic region");
    }
    // The marker is searched for after the fixed fields, since the adaptive
    // template pixels could hold its bytes; the coded data never does, because
    // the coder stuffs a bit after every 0xFF byte and MMR holds no run of 16
    // zero bits.
    Cursor fixed(data, describe_segment, parsed.end + k_region_info_size);
    const std::uint32_t region_flags = fixed.u8();
    const bool mmr = (region_flags & 1U) != 0;
    const unsigned template_index = (region_flags >> 1U) & 3U;
    const std::size_t adaptive_bytes = mmr ? 0 : (template_index == 0 ? 8 : 2);
    const std::uint8_t first = mmr ? 0x00 : 0xff;
    const std::uint8_t second = mmr ? 0x00 : 0xac;
    bool found = false;
    for (std::size_t i = fixed.position() + adaptive_bytes; i + 6 <= data.size(); ++i) {
      if (std::to_integer<std::uint8_t>(data[i]) == first &&
          std::to_integer<std::uint8_t>(data[i + 1]) == second) {
        header.unknown_length = true;
        header.row_count = platform::read_be<std::uint32_t>(data, i + 2).value_or(0);
        parsed.length = (i - parsed.end) + 6;
        found = true;
        break;
      }
    }
    if (!found) {
      throw DecodeFault(describe_segment + " has an unknown length and no end marker");
    }
  }
  return parsed;
}

}  // namespace

const char* Jbig2Segments::type_name(unsigned type) noexcept {
  switch (type) {
    case symbol_dictionary:
      return "SymbolDictionary";
    case intermediate_text_region:
      return "IntermediateTextRegion";
    case immediate_text_region:
      return "ImmediateTextRegion";
    case immediate_lossless_text_region:
      return "ImmediateLosslessTextRegion";
    case pattern_dictionary:
      return "PatternDictionary";
    case intermediate_halftone_region:
      return "IntermediateHalftoneRegion";
    case immediate_halftone_region:
      return "ImmediateHalftoneRegion";
    case immediate_lossless_halftone_region:
      return "ImmediateLosslessHalftoneRegion";
    case intermediate_generic_region:
      return "IntermediateGenericRegion";
    case immediate_generic_region:
      return "ImmediateGenericRegion";
    case immediate_lossless_generic_region:
      return "ImmediateLosslessGenericRegion";
    case intermediate_refinement_region:
      return "IntermediateGenericRefinementRegion";
    case immediate_refinement_region:
      return "ImmediateGenericRefinementRegion";
    case immediate_lossless_refinement_region:
      return "ImmediateLosslessGenericRefinementRegion";
    case page_information:
      return "PageInformation";
    case end_of_page:
      return "EndOfPage";
    case end_of_stripe:
      return "EndOfStripe";
    case end_of_file:
      return "EndOfFile";
    case profiles:
      return "Profiles";
    case tables:
      return "Tables";
    case extension:
      return "Extension";
    default:
      return "unknown";
  }
}

std::vector<Jbig2Segments::Header> Jbig2Segments::read(platform::ByteSpan data) {
  std::size_t position = 0;
  bool random_access = false;
  if (data.size() >= k_file_header_id.size() &&
      std::equal(k_file_header_id.begin(), k_file_header_id.end(), data.begin(),
                 [](std::uint8_t expected, std::byte actual) {
                   return expected == std::to_integer<std::uint8_t>(actual);
                 })) {
    Cursor cursor(data, "the JBIG2 file header", k_file_header_id.size());
    const std::uint32_t flags = cursor.u8();
    random_access = (flags & 1U) == 0;
    if ((flags & 2U) == 0) {
      cursor.skip(4);
    }
    position = cursor.position();
  }

  std::vector<ParsedHeader> parsed;
  while (position < data.size()) {
    ParsedHeader next = parse_header(data, position);
    position = next.end;
    const bool last = next.header.type == end_of_file;
    if (!random_access) {
      const std::uint64_t available = data.size() - position;
      const std::uint64_t length = std::min(next.length, available);
      next.header.data = data.subspan(position, static_cast<std::size_t>(length));
      position += static_cast<std::size_t>(length);
    }
    parsed.push_back(std::move(next));
    if (last) {
      break;
    }
  }
  if (random_access) {
    for (ParsedHeader& entry : parsed) {
      const std::uint64_t available = position <= data.size() ? data.size() - position : 0;
      const std::uint64_t length = std::min(entry.length, available);
      entry.header.data = data.subspan(position, static_cast<std::size_t>(length));
      position += static_cast<std::size_t>(length);
    }
  }
  std::vector<Header> headers;
  headers.reserve(parsed.size());
  for (ParsedHeader& entry : parsed) {
    headers.push_back(std::move(entry.header));
  }
  return headers;
}

Jbig2Segments::PageInfo Jbig2Segments::read_page_information(const Header& header) {
  Cursor cursor(header.data, describe(header) + " data");
  PageInfo info;
  info.width = cursor.u32();
  info.height = cursor.u32();
  cursor.skip(8);  // the horizontal and vertical resolution
  const std::uint32_t flags = cursor.u8();
  info.default_pixel = (flags >> 2U) & 1U;
  info.combination_operator = (flags >> 3U) & 3U;
  info.combination_operator_override = (flags & 64U) != 0;
  return info;
}

Jbig2Segments::GenericRegion Jbig2Segments::read_generic_region(const Header& header) {
  Cursor cursor(header.data, describe(header) + " data");
  GenericRegion region;
  region.info = read_region_info(cursor);
  const std::uint32_t flags = cursor.u8();
  region.mmr = (flags & 1U) != 0;
  region.template_index = (flags >> 1U) & 3U;
  region.prediction = (flags & 8U) != 0;
  if (!region.mmr) {
    region.at = read_adaptive_pixels(cursor, region.template_index == 0 ? 4U : 1U);
  }
  region.coded = rest(header, cursor);
  if (header.unknown_length) {
    region.info.height = std::min(region.info.height, header.row_count);
  }
  return region;
}

Jbig2Segments::RefinementRegion Jbig2Segments::read_refinement_region(const Header& header) {
  Cursor cursor(header.data, describe(header) + " data");
  RefinementRegion region;
  region.info = read_region_info(cursor);
  const std::uint32_t flags = cursor.u8();
  region.template_index = flags & 1U;
  region.prediction = (flags & 2U) != 0;
  if (region.template_index == 0) {
    region.at = read_adaptive_pixels(cursor, 2);
  }
  region.coded = rest(header, cursor);
  return region;
}

Jbig2Segments::SymbolDictionary Jbig2Segments::read_symbol_dictionary(const Header& header) {
  Cursor cursor(header.data, describe(header) + " data");
  SymbolDictionary dictionary;
  const std::uint32_t flags = cursor.u16();
  dictionary.huffman = (flags & 1U) != 0;
  dictionary.refinement = (flags & 2U) != 0;
  dictionary.dh_selector = (flags >> 2U) & 3U;
  dictionary.dw_selector = (flags >> 4U) & 3U;
  dictionary.bitmap_size_selector = (flags >> 6U) & 1U;
  dictionary.aggregation_selector = (flags >> 7U) & 1U;
  dictionary.context_used = (flags & 256U) != 0;
  dictionary.context_retained = (flags & 512U) != 0;
  dictionary.template_index = (flags >> 10U) & 3U;
  dictionary.refinement_template = (flags >> 12U) & 1U;
  if (!dictionary.huffman) {
    dictionary.at = read_adaptive_pixels(cursor, dictionary.template_index == 0 ? 4U : 1U);
  }
  if (dictionary.refinement && dictionary.refinement_template == 0) {
    dictionary.refinement_at = read_adaptive_pixels(cursor, 2);
  }
  dictionary.exported_symbols = cursor.u32();
  dictionary.new_symbols = cursor.u32();
  dictionary.coded = rest(header, cursor);
  return dictionary;
}

Jbig2Segments::TextRegion Jbig2Segments::read_text_region(const Header& header) {
  Cursor cursor(header.data, describe(header) + " data");
  TextRegion region;
  region.info = read_region_info(cursor);
  const std::uint32_t flags = cursor.u16();
  region.huffman = (flags & 1U) != 0;
  region.refinement = (flags & 2U) != 0;
  region.log_strip_size = (flags >> 2U) & 3U;
  region.strip_size = 1U << region.log_strip_size;
  region.reference_corner = (flags >> 4U) & 3U;
  region.transposed = (flags & 64U) != 0;
  region.combination_operator = (flags >> 7U) & 3U;
  region.default_pixel = (flags >> 9U) & 1U;
  // The five-bit signed SBDSOFFSET.
  const int ds_offset = static_cast<int>((flags >> 10U) & 31U);
  region.ds_offset = ds_offset >= 16 ? ds_offset - 32 : ds_offset;
  region.refinement_template = (flags >> 15U) & 1U;
  if (region.huffman) {
    const std::uint32_t huffman_flags = cursor.u16();
    region.huffman_fs = huffman_flags & 3U;
    region.huffman_ds = (huffman_flags >> 2U) & 3U;
    region.huffman_dt = (huffman_flags >> 4U) & 3U;
    region.huffman_rdw = (huffman_flags >> 6U) & 3U;
    region.huffman_rdh = (huffman_flags >> 8U) & 3U;
    region.huffman_rdx = (huffman_flags >> 10U) & 3U;
    region.huffman_rdy = (huffman_flags >> 12U) & 3U;
    region.huffman_rsize = (huffman_flags >> 14U) & 1U;
  }
  if (region.refinement && region.refinement_template == 0) {
    region.refinement_at = read_adaptive_pixels(cursor, 2);
  }
  region.symbol_instances = cursor.u32();
  region.coded = rest(header, cursor);
  return region;
}

Jbig2Segments::PatternDictionary Jbig2Segments::read_pattern_dictionary(const Header& header) {
  Cursor cursor(header.data, describe(header) + " data");
  PatternDictionary dictionary;
  const std::uint32_t flags = cursor.u8();
  dictionary.mmr = (flags & 1U) != 0;
  dictionary.template_index = (flags >> 1U) & 3U;
  dictionary.pattern_width = cursor.u8();
  dictionary.pattern_height = cursor.u8();
  dictionary.max_pattern_index = cursor.u32();
  dictionary.coded = rest(header, cursor);
  return dictionary;
}

Jbig2Segments::HalftoneRegion Jbig2Segments::read_halftone_region(const Header& header) {
  Cursor cursor(header.data, describe(header) + " data");
  HalftoneRegion region;
  region.info = read_region_info(cursor);
  const std::uint32_t flags = cursor.u8();
  region.mmr = (flags & 1U) != 0;
  region.template_index = (flags >> 1U) & 3U;
  region.enable_skip = (flags & 8U) != 0;
  region.combination_operator = (flags >> 4U) & 7U;
  region.default_pixel = (flags >> 7U) & 1U;
  region.grid_width = cursor.u32();
  region.grid_height = cursor.u32();
  region.grid_offset_x = cursor.i32();
  region.grid_offset_y = cursor.i32();
  region.grid_vector_x = cursor.u16();
  region.grid_vector_y = cursor.u16();
  region.coded = rest(header, cursor);
  return region;
}

std::uint32_t Jbig2Segments::read_end_of_stripe(const Header& header) {
  Cursor cursor(header.data, describe(header) + " data");
  return cursor.u32();
}

}  // namespace pdfkit::images
