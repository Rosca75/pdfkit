// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Jbig2Regions holds the decoding procedures of JBIG2 (ITU-T T.88 clauses 6
// and 7.4): generic regions (the four templates with typical prediction, the
// adaptive template pixels and the skip bitmap, or MMR through CcittDecoder),
// generic refinement, symbol dictionaries (arithmetic or Huffman coded, with
// refinement and aggregation), text regions (arithmetic or Huffman coded, with
// refinement), pattern dictionaries and halftone regions. It is a port of the
// decoding functions of pdf.js src/core/jbig2.js at commit
// fe48736846e2043696738bd54435ebb5ab24ecff (Apache-2.0), over the segment
// parameters Jbig2Segments reads. Every bitmap is created through the decode's
// Allowance, so a size claimed by the file is checked against the budget before
// anything is reserved. A damaged stream throws DecodeFault.

#ifndef PDFKIT_IMAGES_JBIG2_REGIONS_HPP
#define PDFKIT_IMAGES_JBIG2_REGIONS_HPP

#include "images/jbig2_bitmap.hpp"
#include "images/jbig2_huffman.hpp"
#include "images/jbig2_segments.hpp"
#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace pdfkit::images {

class Jbig2Regions {
 public:
  using Allowance = Jbig2Bitmap::Allowance;
  // Symbols and patterns are shared between the dictionaries that export them
  // and the regions that use them.
  using BitmapList = std::vector<std::shared_ptr<const Jbig2Bitmap>>;
  // The custom tables a segment refers to, in the order it refers to them.
  using TableList = std::vector<const Jbig2Huffman::Table*>;

  // The context statistics a symbol dictionary leaves for the dictionaries that
  // refer to it (the retained bitmap coding contexts of T.88 7.4.2.1.1).
  struct RetainedContexts {
    std::vector<std::uint8_t> generic;
    std::vector<std::uint8_t> refinement;
  };

  struct Dictionary {
    BitmapList symbols;
    // Set when the dictionary asks for its contexts to be retained.
    std::shared_ptr<const RetainedContexts> retained;
  };

  [[nodiscard]] static Jbig2Bitmap generic_region(const Jbig2Segments::GenericRegion& region,
                                                  Allowance& allowance);

  // Refines reference (the bitmap of the region the segment refers to, or the
  // part of the page it covers) into a bitmap of the region's size.
  [[nodiscard]] static Jbig2Bitmap refinement_region(const Jbig2Segments::RefinementRegion& region,
                                                     const Jbig2Bitmap& reference,
                                                     Allowance& allowance);

  // The exported symbols of a symbol dictionary, given the symbols of the
  // dictionaries it refers to, its custom tables and, when it asks for them,
  // the contexts retained by the last dictionary it refers to.
  [[nodiscard]] static Dictionary symbol_dictionary(
      const Jbig2Segments::SymbolDictionary& dictionary, const BitmapList& input_symbols,
      const TableList& custom_tables, const std::shared_ptr<const RetainedContexts>& inherited,
      Allowance& allowance);

  [[nodiscard]] static Jbig2Bitmap text_region(const Jbig2Segments::TextRegion& region,
                                               const BitmapList& symbols,
                                               const TableList& custom_tables,
                                               Allowance& allowance);

  [[nodiscard]] static BitmapList pattern_dictionary(
      const Jbig2Segments::PatternDictionary& dictionary, Allowance& allowance);

  [[nodiscard]] static Jbig2Bitmap halftone_region(const Jbig2Segments::HalftoneRegion& region,
                                                   const BitmapList& patterns,
                                                   Allowance& allowance);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_JBIG2_REGIONS_HPP
