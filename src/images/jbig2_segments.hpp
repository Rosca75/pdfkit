// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Jbig2Segments reads the segment structure of JBIG2 (ITU-T T.88 clause 7): the
// segment headers of a stream in the embedded organisation PDF uses (or of a
// file with its file header, in either organisation), and the parameter fields
// at the start of each segment type's data. It decodes no image data: every
// parameter struct names the span of coded data that follows its fields. The
// field layouts are those pdf.js reads in src/core/jbig2.js at commit
// fe48736846e2043696738bd54435ebb5ab24ecff (Apache-2.0), checked against the
// standard's clause 7 (recalled). A truncated field or an unknown segment type
// throws DecodeFault naming the segment.

#ifndef PDFKIT_IMAGES_JBIG2_SEGMENTS_HPP
#define PDFKIT_IMAGES_JBIG2_SEGMENTS_HPP

#include "platform/byte_span.hpp"

#include <cstdint>
#include <vector>

namespace pdfkit::images {

class Jbig2Segments {
 public:
  // The segment types of T.88 7.3 this decoder knows by number.
  enum Type : std::uint8_t {
    symbol_dictionary = 0,
    intermediate_text_region = 4,
    immediate_text_region = 6,
    immediate_lossless_text_region = 7,
    pattern_dictionary = 16,
    intermediate_halftone_region = 20,
    immediate_halftone_region = 22,
    immediate_lossless_halftone_region = 23,
    intermediate_generic_region = 36,
    immediate_generic_region = 38,
    immediate_lossless_generic_region = 39,
    intermediate_refinement_region = 40,
    immediate_refinement_region = 42,
    immediate_lossless_refinement_region = 43,
    page_information = 48,
    end_of_page = 49,
    end_of_stripe = 50,
    end_of_file = 51,
    profiles = 52,
    tables = 53,
    extension = 62,
  };

  struct Header {
    std::uint32_t number = 0;
    unsigned type = 0;
    std::vector<std::uint32_t> referred_to;
    std::uint32_t page_association = 0;
    // The segment's data part, after the header.
    platform::ByteSpan data;
    // A generic region of unknown length (7.2.7) ends with the number of rows
    // it holds, which can be fewer than its region information states.
    bool unknown_length = false;
    std::uint32_t row_count = 0;
  };

  // A pixel of a context template at an offset from the pixel being decoded.
  struct AdaptivePixel {
    int x = 0;
    int y = 0;
  };

  struct RegionInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    // 0 OR, 1 AND, 2 XOR, 3 XNOR, 4 REPLACE.
    unsigned combination_operator = 0;
  };

  struct PageInfo {
    std::uint32_t width = 0;
    // 0xFFFFFFFF while the page is striped and its height unknown.
    std::uint32_t height = 0;
    unsigned default_pixel = 0;
    unsigned combination_operator = 0;
    bool combination_operator_override = false;
  };

  struct GenericRegion {
    RegionInfo info;
    bool mmr = false;
    unsigned template_index = 0;
    bool prediction = false;
    std::vector<AdaptivePixel> at;
    platform::ByteSpan coded;
  };

  struct RefinementRegion {
    RegionInfo info;
    unsigned template_index = 0;
    bool prediction = false;
    std::vector<AdaptivePixel> at;
    platform::ByteSpan coded;
  };

  struct SymbolDictionary {
    bool huffman = false;
    bool refinement = false;
    unsigned dh_selector = 0;
    unsigned dw_selector = 0;
    unsigned bitmap_size_selector = 0;
    unsigned aggregation_selector = 0;
    bool context_used = false;
    bool context_retained = false;
    unsigned template_index = 0;
    unsigned refinement_template = 0;
    std::vector<AdaptivePixel> at;
    std::vector<AdaptivePixel> refinement_at;
    std::uint32_t exported_symbols = 0;
    std::uint32_t new_symbols = 0;
    platform::ByteSpan coded;
  };

  struct TextRegion {
    RegionInfo info;
    bool huffman = false;
    bool refinement = false;
    unsigned log_strip_size = 0;
    unsigned strip_size = 1;
    unsigned reference_corner = 0;
    bool transposed = false;
    unsigned combination_operator = 0;
    unsigned default_pixel = 0;
    int ds_offset = 0;
    unsigned refinement_template = 0;
    unsigned huffman_fs = 0;
    unsigned huffman_ds = 0;
    unsigned huffman_dt = 0;
    unsigned huffman_rdw = 0;
    unsigned huffman_rdh = 0;
    unsigned huffman_rdx = 0;
    unsigned huffman_rdy = 0;
    unsigned huffman_rsize = 0;
    std::vector<AdaptivePixel> refinement_at;
    std::uint32_t symbol_instances = 0;
    platform::ByteSpan coded;
  };

  struct PatternDictionary {
    bool mmr = false;
    unsigned template_index = 0;
    unsigned pattern_width = 0;
    unsigned pattern_height = 0;
    std::uint32_t max_pattern_index = 0;
    platform::ByteSpan coded;
  };

  struct HalftoneRegion {
    RegionInfo info;
    bool mmr = false;
    unsigned template_index = 0;
    bool enable_skip = false;
    unsigned combination_operator = 0;
    unsigned default_pixel = 0;
    std::uint32_t grid_width = 0;
    std::uint32_t grid_height = 0;
    std::int32_t grid_offset_x = 0;
    std::int32_t grid_offset_y = 0;
    unsigned grid_vector_x = 0;
    unsigned grid_vector_y = 0;
    platform::ByteSpan coded;
  };

  // The segments of one stream. Data starting with the JBIG2 file header is a
  // file in the sequential or the random-access organisation; anything else is
  // read as the embedded organisation, a plain run of headers each followed by
  // its data. Reading stops after an end-of-file segment.
  [[nodiscard]] static std::vector<Header> read(platform::ByteSpan data);

  // The name of a segment type for a fault message, such as
  // "ImmediateGenericRegion", or "unknown" for a number the standard leaves
  // unassigned.
  [[nodiscard]] static const char* type_name(unsigned type) noexcept;

  [[nodiscard]] static PageInfo read_page_information(const Header& header);
  [[nodiscard]] static GenericRegion read_generic_region(const Header& header);
  [[nodiscard]] static RefinementRegion read_refinement_region(const Header& header);
  [[nodiscard]] static SymbolDictionary read_symbol_dictionary(const Header& header);
  [[nodiscard]] static TextRegion read_text_region(const Header& header);
  [[nodiscard]] static PatternDictionary read_pattern_dictionary(const Header& header);
  [[nodiscard]] static HalftoneRegion read_halftone_region(const Header& header);
  // The row at which an end-of-stripe segment says the stripe ends.
  [[nodiscard]] static std::uint32_t read_end_of_stripe(const Header& header);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_JBIG2_SEGMENTS_HPP
