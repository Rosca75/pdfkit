// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Repair is the reconstruction scan of docs/DESIGN.md section 1.6: one forward
// pass over the whole file for "n g obj" at a token boundary, "trailer" and
// the cross-reference streams, the last occurrence of an object number
// winning, because an incremental update appends the newer version (pdf.js's
// xref.js indexObjects, PDFium's cpdf_parser.cpp rebuild and MuPDF's
// pdf-repair.c, each read in the design record; MuPDF technique only, AGPL, no
// code taken). An object whose parse needed no recovery is skipped whole, so
// the bytes of a stream are never mistaken for objects; after a recovery the
// scan resumes right after the object's header. The document turns what the
// scan found into a table: it opens the object streams, rebuilds the trailer
// and picks the catalog, the information dictionary and the encryption
// dictionary where the trailer lacks them.

#ifndef PDFKIT_SYNTAX_REPAIR_HPP
#define PDFKIT_SYNTAX_REPAIR_HPP

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace pdfkit::syntax {

class Repair {
 public:
  struct Location {
    std::uint64_t offset = 0;
    std::uint32_t generation = 0;
  };

  struct Found {
    // The last occurrence of each object number.
    std::map<std::uint32_t, Location> objects;
    // Every trailer dictionary and cross-reference stream dictionary, in file
    // order.
    std::vector<Dictionary> trailers;
    // Whether "%%EOF" ends the scanned bytes' last revision.
    bool eof_found = false;
  };

  // Scans file (the bytes from the header on).
  [[nodiscard]] static Found scan(platform::ByteSpan file);
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_REPAIR_HPP
