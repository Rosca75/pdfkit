// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FormSplitter decides how a located edit inside a form XObject reaches one
// draw and no other (docs/DESIGN.md section 12). One occurrence is one draw
// on one page (docs/ABI.md section 4.11): when the form is drawn more than
// once in the written pages (on other pages, or twice on one), the page's
// draw is pointed at a copy of the form that holds the edit, and every other
// draw keeps the original; a form nested in a form is copied along its path,
// each copy's Do renamed to the copy below it. A form drawn once is edited in
// place, because a copy would leave the original, unedited and named in the
// resources, in the file. draws() counts the draws of every form over the
// written pages by lexing their content streams and the forms they draw;
// plan() turns one page's splits into the copies to make, the Do operators
// to rename and the resource names to add, which TextRedactor::apply writes.

#ifndef PDFKIT_REDACT_FORM_SPLITTER_HPP
#define PDFKIT_REDACT_FORM_SPLITTER_HPP

#include "redact/text_plan.hpp"
#include "syntax/document.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pdfkit::redact {

class FormSplitter {
 public:
  // Per form object number, how many times the pages draw it, forms drawn
  // by forms included (a form drawn twice by a form drawn on three pages is
  // drawn six times).
  [[nodiscard]] static std::map<std::uint32_t, std::size_t> draws(
      syntax::Document& document, const std::set<std::uint32_t>& pages);

  // One copy to make: the walk it serves, its form, the name the drawing
  // Do takes, and where that Do is.
  struct Copy {
    std::size_t walk = 0;
    std::uint32_t form = 0;
    std::string name;
    std::size_t parent = 0;
  };

  struct Plan {
    // Children before parents, so a copy's own copies exist when it is
    // written.
    std::vector<Copy> copies;
    // Per walk (0 for the page's own content), the changes its stream takes
    // beyond the form's shared target: the located edits and the renamed
    // Do operators. A walk not copied takes them in place.
    std::map<std::size_t, TextPlan::Target> local;
    // Per holder walk (0 for the page's resources), the XObject names to
    // add, each naming the walk whose copy it draws.
    std::map<std::size_t, std::map<std::string, std::size_t>> names;
    // The walks copied.
    std::set<std::size_t> copied;
  };

  // The splits of page number's work, given every form's draws.
  [[nodiscard]] static Plan plan(std::uint32_t number, const TextPlan::PageWork& work,
                                 const std::map<std::uint32_t, std::size_t>& draws);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_FORM_SPLITTER_HPP
