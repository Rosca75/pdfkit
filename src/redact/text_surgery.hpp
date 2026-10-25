// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TextSurgery removes glyphs from one content stream walk, docs/DESIGN.md
// section 12: every text-showing operator holding a removed glyph is
// rebuilt, every other byte of the stream is copied as it was through
// text::ContentSerialiser. The technique is the one MuPDF's redaction filter
// uses (pdf-op-filter.c, AGPL, technique read in the design record, no code
// taken): a removed code leaves the string and its displacement becomes a
// TJ number, -tx / (Tfs * Th) * 1000 with tx the glyph's own advance (Tc and
// Tw in, the TJ number before it left in place, ISO 32000-1 section 9.4.4,
// read), merged with the numbers beside it, so every kept glyph lands where
// it was. A Tj is rebuilt as the TJ of its kept pieces and numbers rather
// than re-anchored with Td or Tm, because both of those also set the line
// matrix a later T*, ' or " reads; ' becomes T* and the TJ, and " becomes Tw,
// Tc, T* and the TJ, so the line advance and the spacing it set survive. An
// operator whose codes are all removed becomes the TJ of one number, which
// keeps the text position the operators after it start from. Codes are cut
// on the boundaries the font's decoding gave each record, so a multi-byte
// code is never split. An operator whose codes cannot be placed on their
// operand bytes (a record carrying the operator's whole range, or a code
// that does not read back from the operand) loses every glyph instead, which
// is noted: a glyph kept by mistake would be a leak, one removed by mistake
// is not.

#ifndef PDFKIT_REDACT_TEXT_SURGERY_HPP
#define PDFKIT_REDACT_TEXT_SURGERY_HPP

#include "text/content_stream.hpp"
#include "text/glyph_record.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace pdfkit::redact {

class TextSurgery {
 public:
  struct Plan {
    // Operation index to the bytes that replace the whole operation.
    std::map<std::size_t, std::string> edits;
    // The glyphs removed beyond those asked, because their operator could
    // not be cut code by code (draw indices), and why.
    std::set<std::size_t> widened;
    std::vector<std::string> notes;
  };

  // The edits that remove the records whose draw index is in removed from
  // walk number stream_index of a page whose records are records.
  [[nodiscard]] static Plan plan(const text::ContentStream& stream, std::size_t stream_index,
                                 std::span<const text::GlyphRecord> records,
                                 const std::set<std::size_t>& removed);

  // The TJ number that moves the text position as far as record's own
  // advance; zero when the font size or the scaling is zero.
  [[nodiscard]] static double hole_number(const text::GlyphRecord& record);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_TEXT_SURGERY_HPP
