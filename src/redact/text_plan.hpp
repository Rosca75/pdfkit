// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TextPlan is the text-redaction part of an edit plan (docs/DESIGN.md
// sections 11 to 13): the caller's matches, the occurrences found on each
// page, the strings the self-check searches the output for, and per page the
// work the write applies, computed while the page was read so the write reads
// no page again. A page's work names the content streams to rewrite (the
// page's own content and the forms it draws) by what to change in each: the
// text-showing operators holding a removed glyph, with every record of each
// such operator (a code is cut by walking the whole operand), the removed
// codes by their operator and first byte, the placeholders to draw after an
// operator, and the marked-content operators to write again. It also names
// the resource dictionaries that gain the placeholder font or a rewritten
// property list, the property lists to replace by object number, the device
// rectangles the redaction changes on the page and the placeholders drawn.
// A glyph-range match is an expression at a location (docs/DESIGN.md
// section 18): its ranges, all on one page, sorted and disjoint, are kept
// with the text they read, the boxes of the glyphs they remove and the codes
// removed, so the self-check verifies it where it was and nowhere else.
// A located match's change inside a form XObject belongs to that one draw
// of the form (a split): the write points that draw at a copy of the form
// holding the change when the form is drawn more than once, and every other
// draw keeps the original (FormSplitter). An occurrence match names one
// occurrence of a text on a page by the narrowings of docs/ABI.md section
// 4.11 and resolves to the glyph ranges pdfkit_find reports for it.

#ifndef PDFKIT_REDACT_TEXT_PLAN_HPP
#define PDFKIT_REDACT_TEXT_PLAN_HPP

#include "redact/match_finder.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "text/glyph_record.hpp"
#include "text/rect.hpp"
#include "writer/placeholder_font.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::redact {

class TextPlan {
 public:
  // An operator's place in a stream: its index and the first byte of a code.
  using Key = std::pair<std::size_t, std::size_t>;

  struct Insertion {
    // The operation the placeholder is drawn after.
    std::size_t after = 0;
    std::string operators;
  };

  // What to change in one content stream.
  struct Target {
    // Every record of each text-showing operator that loses a glyph.
    std::map<std::size_t, std::vector<text::GlyphRecord>> operations;
    // The removed codes.
    std::set<Key> removed;
    // The placeholders, by the key of the glyph they start at.
    std::map<Key, Insertion> insertions;
    // Marked-content operations written again.
    std::map<std::size_t, std::string> replaced;
    // A comment of the stream, or bytes its lexer skipped, spell a redacted
    // string, so the stream is written again without them.
    bool comments = false;

    [[nodiscard]] bool empty() const noexcept {
      return removed.empty() && insertions.empty() && replaced.empty() && !comments;
    }
    // Adds other's changes; this target's keep the place where both have one.
    void merge(const Target& other);
  };

  // A resource dictionary: the page's (form 0) or a form's own.
  using Holder = std::uint32_t;

  // One walk of a form on a page that a located match changes, or that
  // draws such a walk: its form, the walk whose Do drew it and that Do's
  // operation index, and whether the form has resources of its own.
  struct Walk {
    std::uint32_t form = 0;
    std::size_t parent = 0;
    std::size_t drawn_at = 0;
    bool resources = false;
  };

  struct PageWork {
    Target content;
    std::map<std::uint32_t, Target> forms;
    std::set<std::pair<Holder, writer::PlaceholderFont::Face>> fonts;
    // The faces a form without resources of its own draws, which every page
    // drawing the form must name.
    std::map<std::uint32_t, std::set<writer::PlaceholderFont::Face>> form_fonts;
    // Property lists set directly in a holder's /Properties, by name.
    std::map<std::pair<Holder, std::string>, syntax::Dictionary> properties;
    // Property lists replaced by object number.
    std::map<std::uint32_t, syntax::Dictionary> property_objects;
    std::vector<text::Rect> rectangles;
    std::size_t placeholders = 0;
    // Operators that could not be cut code by code and lost every glyph.
    std::vector<std::string> notes;
    // The changes of located matches inside forms, by walk: what that one
    // draw changes beyond the form's shared target.
    std::map<std::size_t, Target> splits;
    // Every walk on the form path of a split, by walk index (the page's own
    // content, walk 0, is not listed).
    std::map<std::size_t, Walk> walks;
  };

  // One stretch of a page's reading-order glyph list, [first, last).
  struct Range {
    std::uint32_t page = 0;
    std::size_t first = 0;
    std::size_t last = 0;
  };

  // A glyph-range match: one expression at one place.
  struct Located {
    // The match it is (its index in matches).
    std::size_t match = 0;
    std::uint32_t page = 0;
    // Sorted and disjoint.
    std::vector<Range> ranges;
    // The ranges' text as the extraction reads it, the ranges joined by a
    // space.
    std::string text;
    // The device boxes of every glyph the match removes (its duplicates and
    // the glyphs of an /ActualText span it reaches included).
    std::vector<text::Rect> boxes;
    // The removed codes per font run, hexadecimal, as pdfkit_redact_text
    // reports them.
    std::vector<std::string> codes;
  };

  // An occurrence match: the text, its page and folding, and the
  // narrowings, at least one of them given.
  struct OccurrenceQuery {
    std::string text;
    std::uint32_t page = 0;
    MatchFinder::Folding folding;
    std::optional<std::size_t> index;
    std::optional<text::Rect> near;
    std::optional<std::string> before;
    std::optional<std::string> after;
  };

  // The glyph ranges of the one occurrence query names on its page, in
  // pdfkit_find's order (page reading order, then first glyph), every
  // narrowing applying. Throws platform::Error: invalid_argument for a page
  // outside the document, occurrence_not_found when no occurrence is left,
  // occurrence_ambiguous when several are (naming how many and the index of
  // each). where names the match in the message.
  [[nodiscard]] static std::vector<Range> resolve(syntax::Document& document,
                                                  const OccurrenceQuery& query,
                                                  const std::string& where);

  // The ranges of one located match checked and sorted: at least one, all
  // on one page, none empty and no two overlapping. Throws platform::Error
  // (invalid_argument) naming where (the match) otherwise. Whether a range
  // lies within its page is checked when the page is read.
  [[nodiscard]] static std::vector<Range> located_ranges(std::vector<Range> ranges,
                                                         const std::string& where);

  std::vector<MatchFinder::Match> matches;
  // Every occurrence found, by page, before the overlap rule.
  std::map<std::uint32_t, std::vector<MatchFinder::Occurrence>> occurrences;
  // The values the self-check searches every surface for: each search
  // string.
  std::vector<MatchFinder::Match> needles;
  // The glyph-range matches, verified at their location.
  std::vector<Located> located;
  std::map<std::uint32_t, PageWork> pages;
  // The forms each page read draws, by object number.
  std::map<std::uint32_t, std::set<std::uint32_t>> forms_drawn;
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_TEXT_PLAN_HPP
