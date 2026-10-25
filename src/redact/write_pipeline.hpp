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
// (section 14), the metadata stage only for field values (section 10), and
// the self-check (section 18) only when a text match was redacted, which the
// report says with its reason.

#ifndef PDFKIT_REDACT_WRITE_PIPELINE_HPP
#define PDFKIT_REDACT_WRITE_PIPELINE_HPP

#include "redact/edit_plan.hpp"
#include "writer/object_graph.hpp"
#include "writer/write_report.hpp"

#include <string>

namespace pdfkit::redact {

class WritePipeline {
 public:
  struct Output {
    std::string pdf;
    writer::WriteReport report;
  };

  // Writes the document of plan with the plan applied.
  [[nodiscard]] static Output run(EditPlan& plan);

 private:
  static void select_pages(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report);
  static void text_surgery(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report);
  static void image_treatments(EditPlan& plan, writer::ObjectGraph& graph,
                               writer::WriteReport& report);
  static void metadata(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report);
  static void strip_channels(writer::ObjectGraph& graph, writer::WriteReport& report);
  static std::string serialise(writer::ObjectGraph& graph, writer::WriteReport& report);
  static void self_check(EditPlan& plan, const std::string& pdf, writer::WriteReport& report);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_WRITE_PIPELINE_HPP
