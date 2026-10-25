// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The stages in their order. The graph starts as the source document and each
// stage edits it; the source document itself is never changed, so one plan
// written twice gives the same bytes.

#include "redact/write_pipeline.hpp"

#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "writer/channel_table.hpp"
#include "writer/file_writer.hpp"
#include "writer/page_selector.hpp"

#include <utility>

namespace pdfkit::redact {

WritePipeline::Output WritePipeline::run(EditPlan& plan) {
  Output output;
  {
    // What the output no longer carries is counted in the source, before any
    // stage edits it, so a channel only a removed page held is counted too.
    writer::ObjectGraph source(plan.document());
    writer::ChannelTable::Report found = writer::ChannelTable::detect(source);
    output.report.channels = std::move(found.counts);
    output.report.pending_redactions = found.pending_redactions;
    output.report.warnings = std::move(found.warnings);
  }
  writer::ObjectGraph graph(plan.document());
  select_pages(plan, graph, output.report);
  text_surgery(plan, graph, output.report);
  image_treatments(plan, graph, output.report);
  metadata(plan, graph, output.report);
  strip_channels(graph, output.report);
  output.pdf = serialise(graph, output.report);
  self_check(plan, output.pdf, output.report);
  return output;
}

void WritePipeline::select_pages(EditPlan& plan, writer::ObjectGraph& graph,
                                 writer::WriteReport& report) {
  const std::vector<std::uint32_t> none;
  const writer::PageSelector::Result selected =
      writer::PageSelector::apply(graph, plan.pages() ? *plan.pages() : none);
  report.pages = selected.pages;
  report.unpruned_resources = selected.unpruned;
}

void WritePipeline::text_surgery(EditPlan& /*plan*/, writer::ObjectGraph& /*graph*/,
                                 writer::WriteReport& /*report*/) {
  // The plan holds no text match: no content stream is edited.
}

void WritePipeline::image_treatments(EditPlan& /*plan*/, writer::ObjectGraph& /*graph*/,
                                     writer::WriteReport& /*report*/) {
  // The plan holds no image treatment: every image is kept.
}

void WritePipeline::metadata(EditPlan& /*plan*/, writer::ObjectGraph& /*graph*/,
                             writer::WriteReport& /*report*/) {
  // The plan holds no metadata value: the Info dictionary and the XMP packet
  // keep their values (the channel table still strips the XMP history).
}

void WritePipeline::strip_channels(writer::ObjectGraph& graph, writer::WriteReport& report) {
  writer::ChannelTable::Report stripped = writer::ChannelTable::strip(graph, report.pages);
  // The warnings of the strip itself (an XMP packet that does not scan); the
  // pending redactions were reported from the source.
  for (writer::ChannelTable::Warning& warning : stripped.warnings) {
    if (warning.code != "pending_redactions") {
      report.warnings.push_back(std::move(warning));
    }
  }
}

std::string WritePipeline::serialise(writer::ObjectGraph& graph, writer::WriteReport& report) {
  writer::FileWriter::Result written = writer::FileWriter::write(graph);
  report.objects = written.objects;
  report.missing_references = written.missing_references;
  report.bytes = written.bytes.size();
  const auto hash = platform::crypto::Sha2::digest(platform::crypto::Sha2::Variant::sha256,
                                                   platform::as_bytes(written.bytes));
  constexpr std::string_view hex = "0123456789abcdef";
  for (const std::byte value : hash) {
    const auto byte = std::to_integer<unsigned>(value);
    report.sha256 += hex[byte >> 4U];
    report.sha256 += hex[byte & 0x0FU];
  }
  return std::move(written.bytes);
}

void WritePipeline::self_check(EditPlan& /*plan*/, const std::string& /*pdf*/,
                               writer::WriteReport& report) {
  report.self_check_status = "not_run";
  report.self_check_reason = "no text redaction in the edit plan";
}

}  // namespace pdfkit::redact
