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

#ifndef PDFKIT_REDACT_TEXT_PLAN_HPP
#define PDFKIT_REDACT_TEXT_PLAN_HPP

#include "redact/match_finder.hpp"
#include "syntax/dictionary.hpp"
#include "text/glyph_record.hpp"
#include "text/rect.hpp"
#include "writer/placeholder_font.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
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

    [[nodiscard]] bool empty() const noexcept {
      return removed.empty() && insertions.empty() && replaced.empty();
    }
    // Adds other's changes; this target's keep the place where both have one.
    void merge(const Target& other);
  };

  // A resource dictionary: the page's (form 0) or a form's own.
  using Holder = std::uint32_t;

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
  };

  std::vector<MatchFinder::Match> matches;
  // Every occurrence found, by page, before the overlap rule.
  std::map<std::uint32_t, std::vector<MatchFinder::Occurrence>> occurrences;
  // The strings the self-check searches for: each search string, and the
  // text of each glyph range as the extraction read it.
  std::vector<MatchFinder::Match> needles;
  std::map<std::uint32_t, PageWork> pages;
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_TEXT_PLAN_HPP
