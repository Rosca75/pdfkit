// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// SelfCheck re-reads an output with the library's own reader and verifies
// each kind of request where it applies, docs/DESIGN.md section 18, so a
// redaction that left what it removed is refused rather than handed back:
//   - a value (a search string) is searched for in every surface below,
//     under its folding;
//   - a located match (a glyph range: one expression at one place) is
//     verified at that place ("location"): no occurrence of its text, in any
//     order a reader uses, has a glyph whose centre lies in the box of a
//     glyph the match removed; no string operator drawing a glyph there
//     holds the codes the match removed; no /ActualText, /Alt or /E of a
//     marked-content span drawing a glyph there holds its text. Nothing
//     elsewhere is searched for it;
//   - an original value of a rewritten metadata field is searched for in the
//     Info dictionary, the XMP packet and the string objects, whatever its
//     length, and not in the page text, where the document may say it for
//     another reason.
// The surfaces of a value:
//   - the text of every page in reading order and in every other order a
//     reader uses (MatchFinder::find_orders), hidden blocks included
//     ("text"), because a phrase another reader puts together survives for
//     that reader; an occurrence drawn wholly in the placeholder font is the
//     placeholder itself and does not count;
//   - every /ActualText, /Alt and /E of marked content ("actual_text",
//     "alt", "expansion");
//   - the Info dictionary's strings ("info") and the text of the catalog's
//     XMP packet, its markup left out ("xmp");
//   - every other string object in the file ("string"), less the strings of
//     font dictionaries (a CIDSystemInfo's registry and ordering), which are
//     the font format's vocabulary;
//   - the decoded bytes of every stream, scanned raw for the string's UTF-8,
//     PDFDocEncoding and UTF-16BE forms ("stream"): a content stream (a
//     page's contents, a form, a pattern, a Type 3 glyph) by the bytes of its
//     string operands, its comments, the bytes its lexer skipped and the
//     operators Annex A does not know, because its operator names, resource
//     names and marked-content tags are the producer's vocabulary, not the
//     document's;
//     every other stream whole, except a font program or a font's CMap,
//     image samples, an ICC profile, a function, and the XMP packet, which is
//     searched as text above.
// Before a surface is searched, every placeholder of the plan is cut out of
// it, so a placeholder that happens to contain a redacted string is not a
// survivor. The re-read is bounded by the limits the input was opened with.
// pdfkit_check runs the same search over any bytes with no placeholders.

#ifndef PDFKIT_REDACT_SELF_CHECK_HPP
#define PDFKIT_REDACT_SELF_CHECK_HPP

#include "platform/byte_span.hpp"
#include "platform/error.hpp"
#include "redact/edit_plan.hpp"
#include "redact/match_finder.hpp"
#include "syntax/limits.hpp"
#include "text/rect.hpp"
#include "writer/write_report.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace pdfkit::redact {

class SelfCheck {
 public:
  struct Survivor {
    // The redacted string, as given.
    std::string string;
    // The page, when the surface belongs to one.
    std::optional<std::uint32_t> page;
    std::string surface;
    // The object holding the string, 0 when none does.
    std::uint32_t object = 0;
  };

  // A located match on an output page.
  struct Located {
    std::string text;
    std::uint32_t page = 0;
    // The boxes of the glyphs the match removed.
    std::vector<text::Rect> boxes;
    // The removed codes per font run, hexadecimal.
    std::vector<std::string> codes;
  };

  // What one check verifies, by kind of request.
  struct Needles {
    std::span<const MatchFinder::Match> values;
    std::span<const Located> located;
    // Searched under the default folding.
    std::span<const std::string> metadata;
    // The values the caller wrote into the metadata fields, cut out of the
    // metadata surfaces before the originals are searched, as placeholders
    // are.
    std::span<const std::string> written;
  };

  struct Result {
    std::vector<Survivor> survivors;
    // Every request verified: values, located matches and metadata values.
    std::size_t strings = 0;
    std::size_t located = 0;
    std::size_t metadata_values = 0;
    std::size_t pages = 0;
    // The pages whose text was read.
    std::size_t pages_read = 0;
  };

  // Searches pdf for needles; placeholders are cut out of every surface
  // first. Throws the document's refusal when pdf does not open.
  // pages, when given, names the output pages whose text is read; the
  // others are known to read as their input did, which the redaction
  // already searched with every needle (the write pipeline gives it only
  // for a plan of search strings alone). Every other surface is searched in
  // full.
  [[nodiscard]] static Result run(platform::ByteSpan pdf, const syntax::Limits& limits,
                                  std::span<const MatchFinder::Match> needles,
                                  std::span<const std::string> placeholders,
                                  const std::set<std::uint32_t>* pages = nullptr);
  // The same with every kind of request; the pages of the located matches
  // are read whatever pages names.
  [[nodiscard]] static Result run(platform::ByteSpan pdf, const syntax::Limits& limits,
                                  const Needles& needles, std::span<const std::string> placeholders,
                                  const std::set<std::uint32_t>* pages = nullptr);

  // The self-check stage of the write pipeline: verifies pdf, the output of
  // plan whose pages are report.pages, and records the verdict in report;
  // throws the refusal when anything survives. A plan with no text match
  // and no rewritten metadata field is not checked.
  static void verify(EditPlan& plan, const std::string& pdf, writer::WriteReport& report);

  // The self_check refusal naming the survivors' pages and surfaces.
  [[nodiscard]] static platform::Error refusal(const Result& result);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_SELF_CHECK_HPP
