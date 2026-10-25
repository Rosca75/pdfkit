// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ContentSerialiser writes a content stream walk back to bytes, docs/DESIGN.md
// section 6.3: the bytes between operations as they were, every operation as
// it was written, and every text-showing operation rebuilt around its glyph
// records, each code written from the bytes its record names and the bytes
// between codes (delimiters, TJ numbers, white space) kept. It formats
// nothing: what it is not told to change it copies. Before a code is written
// its bytes are decoded again and compared with the record's code, and the
// records of one operator must follow one another without overlap, so a
// record whose byte range is off by one byte fails here, where every text
// assertion would pass. With no edits the output is the walk's bytes exactly,
// the re-serialisation identity the reading gate holds over every page.
//
// An edit replaces one operation's bytes wholesale (an empty replacement
// removes the operator with its operands); the text surgery computes those
// replacements from the glyph records and writes the stream through here, so
// the bytes it does not touch are copied by the same code the identity
// proves.

#ifndef PDFKIT_TEXT_CONTENT_SERIALISER_HPP
#define PDFKIT_TEXT_CONTENT_SERIALISER_HPP

#include "text/content_stream.hpp"
#include "text/glyph_record.hpp"

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace pdfkit::text {

class ContentSerialiser {
 public:
  struct Result {
    std::vector<std::byte> bytes;
    // Records whose bytes do not decode to their code, or that overlap the
    // record before them, each named.
    std::vector<std::string> mismatches;
  };

  // Writes stream (the walk with index stream_index) back. records are the
  // page's glyph records; those of other walks are ignored. edits maps an
  // operation index to the bytes that replace it.
  [[nodiscard]] static Result write(const ContentStream& stream, std::size_t stream_index,
                                    std::span<const GlyphRecord> records,
                                    const std::map<std::size_t, std::string>& edits = {});
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_CONTENT_SERIALISER_HPP
