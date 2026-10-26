// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageTreatmentApplier is the image-treatment stage of the write pipeline
// (CLAUDE.md section 3.2, docs/DESIGN.md section 14). Every image XObject
// whose identity the plan treats with a box, a blur or region boxes is
// decoded to its samples, painted, mosaicked and captioned there, re-encoded
// and put back under its own object number, so every draw of it, on every
// page and in every form or pattern, reaches the treated pixels and the
// original stream is gone from the output; its soft or stencil mask is
// treated over the same rectangles and replaced the same way. A treated
// inline image is replaced inside the content stream that draws it, its
// dictionary keeping its abbreviated keys. A removed image loses every draw
// and resource entry (ImageRemover), and its object is emptied, so nothing
// left anywhere can still reach its bytes. The write report gains the device
// rectangles of every treated draw on the output's pages, beside the text
// redaction's, and one entry per treated identity.

#ifndef PDFKIT_REDACT_IMAGE_TREATMENT_APPLIER_HPP
#define PDFKIT_REDACT_IMAGE_TREATMENT_APPLIER_HPP

#include "images/image_decoder.hpp"
#include "redact/edit_plan.hpp"
#include "redact/image_treatment_plan.hpp"
#include "writer/object_graph.hpp"
#include "writer/write_report.hpp"

namespace pdfkit::redact {

class ImageTreatmentApplier {
 public:
  // The stage: applies plan's image treatments to graph, whose pages are
  // report.pages, and records what it did in report.
  static void apply(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report);

  // Paints treatment into samples (the mosaics first, then the boxes and
  // their captions).
  static void treat(images::ImageDecoder::Samples& samples,
                    const ImageTreatmentPlan::Treatment& treatment);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_IMAGE_TREATMENT_APPLIER_HPP
