// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The stages in their order. The graph starts as the source document and each
// stage edits it; the source document itself is never changed, so one plan
// written twice gives the same bytes.

#include "redact/write_pipeline.hpp"

#include "platform/byte_span.hpp"
#include "platform/crypto/sha2.hpp"
#include "redact/self_check.hpp"
#include "redact/text_redactor.hpp"
#include "writer/channel_table.hpp"
#include "writer/file_writer.hpp"
#include "writer/metadata_writer.hpp"
#include "writer/page_selector.hpp"

#include <algorithm>
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

void WritePipeline::text_surgery(EditPlan& plan, writer::ObjectGraph& graph,
                                 writer::WriteReport& report) {
  // A plan without text matches edits no content stream.
  TextRedactor::apply(plan, graph, report);
}

void WritePipeline::image_treatments(EditPlan& /*plan*/, writer::ObjectGraph& /*graph*/,
                                     writer::WriteReport& /*report*/) {
  // The plan holds no image treatment: every image is kept.
}

void WritePipeline::metadata(EditPlan& plan, writer::ObjectGraph& graph,
                             writer::WriteReport& report) {
  // Runs on every write: a plan without metadata values still has the
  // packet's identifiers replaced and its media-management history removed.
  writer::MetadataWriter::apply(graph, plan.metadata(), plan.metadata_originals(), report.warnings);
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
  const TextPlan& text = plan.text();
  if (text.matches.empty() && plan.metadata_originals().empty()) {
    report.self_check_status = "not_run";
    report.self_check_reason = "no text redaction in the edit plan";
    return;
  }
  std::set<std::string> unique;
  for (const MatchFinder::Match& match : text.matches) {
    unique.insert(match.placeholder);
  }
  const std::vector<std::string> placeholders(unique.begin(), unique.end());
  // Whether the plan's text matches already search for original with the
  // default folding.
  const auto searched = [&text](const std::string& original) {
    return std::ranges::any_of(text.matches, [&original](const MatchFinder::Match& match) {
      const MatchFinder::Folding defaults;
      return match.kind == MatchFinder::Match::Kind::search && match.search == original &&
             match.folding.hyphenation == defaults.hyphenation &&
             match.folding.text.case_fold == defaults.text.case_fold &&
             match.folding.text.diacritics == defaults.text.diacritics &&
             match.folding.text.whitespace == defaults.text.whitespace;
    });
  };
  // Every original value of a rewritten field is a redacted string under the
  // default folding (docs/DESIGN.md section 18), once.
  std::vector<MatchFinder::Match> needles = text.needles;
  for (const std::string& original : plan.metadata_originals()) {
    if (!searched(original)) {
      MatchFinder::Match needle;
      needle.kind = MatchFinder::Match::Kind::search;
      needle.search = original;
      needles.push_back(std::move(needle));
    }
  }
  // With search strings alone, every source page was searched with every
  // string when the matches were added, so a page the redaction left as it
  // was (no edit, no edited form drawn) reads as it did then and holds no
  // occurrence; its text is not read again. The page text of every changed
  // page and every other surface of every page is. A metadata value the plan
  // does not also search for was searched on no page, so with one every
  // page's text is read.
  std::optional<std::set<std::uint32_t>> changed;
  if (!text.glyph_ranges && std::ranges::all_of(plan.metadata_originals(), searched)) {
    std::set<std::uint32_t> forms;
    for (const auto& [number, work] : text.pages) {
      for (const auto& [form, target] : work.forms) {
        if (!target.empty()) {
          forms.insert(form);
        }
      }
    }
    changed.emplace();
    for (std::size_t i = 0; i < report.pages.size(); ++i) {
      const std::uint32_t source = report.pages[i];
      bool touched = text.pages.contains(source);
      if (const auto drawn = text.forms_drawn.find(source); drawn != text.forms_drawn.end()) {
        for (const std::uint32_t form : drawn->second) {
          touched = touched || forms.contains(form);
        }
      } else {
        touched = true;
      }
      if (touched) {
        changed->insert(static_cast<std::uint32_t>(i + 1));
      }
    }
  }
  const SelfCheck::Result result =
      SelfCheck::run(platform::as_bytes(pdf), plan.document().limits(), needles, placeholders,
                     changed ? &*changed : nullptr);
  report.self_check_strings = result.strings;
  if (!result.survivors.empty()) {
    throw SelfCheck::refusal(result);
  }
  report.self_check_status = "passed";
  report.self_check_reason.clear();
}

}  // namespace pdfkit::redact
