// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The stages in their order. The graph starts as the source document and each
// stage edits it; the source document itself is never changed, so one plan
// written twice gives the same bytes.

#include "redact/write_pipeline.hpp"

#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "redact/image_treatment_applier.hpp"
#include "redact/optional_content_remover.hpp"
#include "redact/self_check.hpp"
#include "redact/text_redactor.hpp"
#include "syntax/document.hpp"
#include "syntax/page_damage.hpp"
#include "text/damage_scan.hpp"
#include "writer/channel_table.hpp"
#include "writer/file_writer.hpp"
#include "writer/metadata_writer.hpp"
#include "writer/page_selector.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

namespace pdfkit::redact {

WritePipeline::Output WritePipeline::run(EditPlan& plan) {
  Output output;
  {
    // The channels are counted in the source, before any stage edits it, so
    // a channel only a removed page held is counted as stripped too.
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
  // The content the default optional-content configuration turns off goes
  // before the channel strip drops /OCProperties and every /OC key, which
  // would show it (docs/DESIGN.md section 15).
  OptionalContentRemover::apply(graph);
  strip_channels(graph, output.report);
  output.pdf = serialise(graph, output.report);
  self_check(plan, output.pdf, output.report);
  damaged_pages(plan, output.pdf, output.report);
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

void WritePipeline::text_surgery(EditPlan& plan, writer::ObjectGraph& graph,
                                 writer::WriteReport& report) {
  // A plan without text matches edits no content stream.
  TextRedactor::apply(plan, graph, report);
}

void WritePipeline::image_treatments(EditPlan& plan, writer::ObjectGraph& graph,
                                     writer::WriteReport& report) {
  // A plan without image treatments keeps every image as it is.
  ImageTreatmentApplier::apply(plan, graph, report);
}

void WritePipeline::metadata(EditPlan& plan, writer::ObjectGraph& graph,
                             writer::WriteReport& report) {
  // Runs on every write: a plan without metadata values still has the
  // packet's identifiers replaced and its media-management history removed.
  // A value outside the field list that repeats a redacted string, a
  // metadata original or a string the text redaction removes, goes.
  std::vector<std::string> redacted = plan.metadata_originals();
  for (const MatchFinder::Match& needle : plan.text().needles) {
    if (!needle.search.empty()) {
      redacted.push_back(needle.search);
    }
  }
  writer::MetadataWriter::apply(graph, plan.metadata(), redacted, report.warnings);
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

void WritePipeline::self_check(EditPlan& plan, const std::string& pdf,
                               writer::WriteReport& report) {
  // Each kind of request is verified where it applies (SelfCheck::verify).
  SelfCheck::verify(plan, pdf, report);
}

void WritePipeline::damaged_pages(EditPlan& plan, const std::string& pdf,
                                  writer::WriteReport& report) {
  // The source pages the output keeps that could not be read in full, read
  // by the scan the document handle keeps.
  std::map<std::uint32_t, syntax::PageDamage> source;
  for (syntax::PageDamage& damage : plan.damage().pages(report.pages)) {
    const std::uint32_t page = damage.page;
    source.emplace(page, std::move(damage));
  }
  if (source.empty()) {
    return;
  }
  const TextPlan& text = plan.text();
  std::set<std::uint32_t> edited;
  for (const auto& [page, work] : text.pages) {
    edited.insert(page);
  }
  for (const TextPlan::Located& entry : text.located) {
    edited.insert(entry.page);
  }
  for (const TextPlan::StringEdit& edit : text.strings) {
    edited.insert(edit.page);
  }
  // The output read again by the same walk: a damaged stream copied as it
  // was, or bytes no operator draws kept by a rewrite, read there as damage
  // again, while a rewrite of what decoded leaves an undecoded tail behind.
  const std::unique_ptr<syntax::Document> output =
      syntax::Document::open(platform::as_bytes(pdf), plan.document().limits());
  text::DamageScan reread(*output);
  for (std::size_t i = 0; i < report.pages.size(); ++i) {
    const auto found = source.find(report.pages[i]);
    if (found == source.end()) {
      continue;
    }
    syntax::PageDamage entry = found->second;
    entry.page = static_cast<std::uint32_t>(i + 1);
    entry.source_page = found->first;
    // A search string is searched on every page; any other match on its own.
    entry.text_redaction = !text.needles.empty() || edited.contains(found->first);
    const std::array<std::uint32_t, 1> page{entry.page};
    entry.copied_as_is = !reread.pages(page).empty();
    report.damaged_pages.push_back(std::move(entry));
  }
}

}  // namespace pdfkit::redact
