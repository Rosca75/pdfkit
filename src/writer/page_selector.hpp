// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PageSelector rebuilds the page tree of an output, docs/DESIGN.md section 16:
// one fresh /Pages node holds the kept pages in the caller's order (every
// page in order for a write without a selection), each page carrying the
// /Resources, /MediaBox, /CropBox and /Rotate it inherited, because the old
// intermediate nodes go. Each kept page's resource dictionary, and the
// resources of every form, tiling pattern and soft-mask group it draws,
// recursively, is pruned to the names its content uses, in a pruned copy, so
// a resource only a removed page drew becomes unreachable and is not written.
// A form, a Type 3 glyph procedure, a pattern or a group without resources of
// its own reads its user's (ISO 32000-1 section 7.8.3 deprecates it and every
// viewer tolerates it), so its names count as its user's. A content stream the
// usage reader could not read whole keeps its resource dictionary whole and is
// counted. A content stream drawn by two kept pages stays one object.

#ifndef PDFKIT_WRITER_PAGE_SELECTOR_HPP
#define PDFKIT_WRITER_PAGE_SELECTOR_HPP

#include "writer/object_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pdfkit::writer {

class PageSelector {
 public:
  struct Result {
    // The kept source page numbers, in output order.
    std::vector<std::uint32_t> pages;
    // Resource dictionaries kept whole because their content did not read
    // whole.
    std::size_t unpruned = 0;
  };

  // Keeps pages (1-based source page numbers, already validated) of graph's
  // document in that order; an empty list keeps every page in order.
  [[nodiscard]] static Result apply(ObjectGraph& graph, const std::vector<std::uint32_t>& pages);
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_PAGE_SELECTOR_HPP
