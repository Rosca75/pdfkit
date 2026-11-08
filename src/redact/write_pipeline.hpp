// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// WritePipeline applies an edit plan in the fixed order of CLAUDE.md section
// 3.2: page selection, text surgery, image treatments, metadata, channel
// stripping, serialisation, then the self-check of the output. Every stage is
// a named step here, so an edit the catalogue adds fills its stage and never
// reorders the pipeline. A stage the plan gives nothing to do leaves the
// output as it found it: the text surgery runs only for text matches
// (docs/DESIGN.md section 12), the image treatments only for treatments
// (section 14), and the self-check (section 18) only when a text match was
// redacted or a metadata field rewritten, which the report says with its
// reason. The metadata stage (section 10) runs on every write, because the
// XMP packet's identifiers and history name the original whatever the plan
// holds. Last, the report names each output page whose source page could not
// be read in full (the damaged pages section), because a verification over
// such a page is incomplete whatever the self-check found.

#ifndef PDFKIT_REDACT_WRITE_PIPELINE_HPP
#define PDFKIT_REDACT_WRITE_PIPELINE_HPP

#include "redact/edit_plan.hpp"
#include "syntax/limits.hpp"
#include "syntax/page_damage.hpp"
#include "writer/object_graph.hpp"
#include "writer/write_report.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::redact {

class WritePipeline {
 public:
  struct Output {
    std::string pdf;
    writer::WriteReport report;
  };

  // Writes the document of plan with the plan applied.
  [[nodiscard]] static Output run(EditPlan& plan);

  // Per output page of pages, whether the output pdf, opened under limits and
  // read again by the content walk, still holds damage there: yes, no, or
  // unknown when it does not open or the page reads only as not checked.
  [[nodiscard]] static std::vector<syntax::PageDamage::Copied> copied_as_is(
      std::string_view pdf, const syntax::Limits& limits, std::span<const std::uint32_t> pages);

 private:
  static void select_pages(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report);
  static void text_surgery(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report);
  static void image_treatments(EditPlan& plan, writer::ObjectGraph& graph,
                               writer::WriteReport& report);
  static void metadata(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report);
  static void strip_channels(writer::ObjectGraph& graph, writer::WriteReport& report);
  static std::string serialise(writer::ObjectGraph& graph, writer::WriteReport& report);
  static void self_check(EditPlan& plan, const std::string& pdf, writer::WriteReport& report);
  static void damaged_pages(EditPlan& plan, const std::string& pdf, writer::WriteReport& report);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_WRITE_PIPELINE_HPP
