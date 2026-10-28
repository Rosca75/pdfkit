// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// TextRedactor is text redaction from the caller's matches to the edited
// output, docs/DESIGN.md sections 11 to 13. add() checks the matches (a
// placeholder outside the alphabet, a glyph range from another document or
// past its page, all refused before anything is recorded), reads the pages
// they concern with the text layer, finds the occurrences (MatchFinder: in
// the reading order, in every other order a reader uses, and in the codes),
// and computes each page's work: the glyphs to remove (a match's glyphs, the
// duplicates folded into them and every glyph of an /ActualText span one of
// them belongs to), the placeholder of each occurrence fitted into its hole
// (PlaceholderFitting) and drawn at its first line (PlaceholderDrawing), the
// text operators rebuilt (TextSurgery), the marked-content properties
// rewritten (MarkedContentRewriter), the forms touched (once per form,
// whatever draws it) and the device rectangles changed. apply() is the text
// surgery stage of the write pipeline: it writes each page's content and
// each touched form again through the object graph, embeds the placeholder
// faces drawn (writer::PlaceholderFont) and names them, with any rewritten
// property list, in the resource dictionaries the streams read.

#ifndef PDFKIT_REDACT_TEXT_REDACTOR_HPP
#define PDFKIT_REDACT_TEXT_REDACTOR_HPP

#include "redact/edit_plan.hpp"
#include "redact/match_finder.hpp"
#include "redact/placeholder_fitting.hpp"
#include "redact/text_plan.hpp"
#include "text/rect.hpp"
#include "writer/object_graph.hpp"
#include "writer/write_report.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pdfkit::redact {

class TextRedactor {
 public:
  // One occurrence as pdfkit_redact_text reports it.
  struct Found {
    std::uint32_t page = 0;
    std::size_t first = 0;
    std::size_t last = 0;
    std::string text;
    std::vector<text::Rect> rectangles;
    bool drawn = false;
    PlaceholderFitting::Outcome fitting = PlaceholderFitting::Outcome::natural;
    double scale = 1.0;
    // The removed glyphs' codes in draw order, as hexadecimal, per font run.
    std::vector<std::string> codes;
    // A located match's ranges, sorted; empty for a search's occurrence.
    std::vector<TextPlan::Range> ranges;
  };

  struct MatchReport {
    std::vector<Found> occurrences;
  };

  // Adds matches to plan, each checked first (platform::Error,
  // invalid_argument); returns per match what was found.
  [[nodiscard]] static std::vector<MatchReport> add(EditPlan& plan,
                                                    std::vector<MatchFinder::Match> matches);
  // The same, ranges[i] holding the ranges of glyph-range match i when the
  // caller gave several (one expression, docs/DESIGN.md section 11): checked
  // by TextPlan::located_ranges, the placeholder drawn at the first and the
  // others holes, answered as one occurrence. An empty entry reads the
  // match's own page, first and last.
  [[nodiscard]] static std::vector<MatchReport> add(
      EditPlan& plan, std::vector<MatchFinder::Match> matches,
      std::vector<std::vector<TextPlan::Range>> ranges);

  // The text surgery stage: applies the plan's page work to graph, whose
  // pages are report.pages, and records the rectangles in report.
  static void apply(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_TEXT_REDACTOR_HPP
