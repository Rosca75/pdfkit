// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The passes of one page and its report. The page's warnings are the
// document's warnings that name the page (a damaged stream, a missing
// resource, met while the page was read) followed by the text layer's own, in
// a fixed order, so two reads of one page report the same lines.

#include "text/text_reader.hpp"

#include "text/actual_text.hpp"
#include "text/hidden_text.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

namespace pdfkit::text {

namespace {

[[nodiscard]] std::string count_of(std::uint64_t count, std::string_view one,
                                   std::string_view many) {
  return std::to_string(count) + " " + std::string(count == 1 ? one : many);
}

}  // namespace

TextReader::TextReader(syntax::Document& document)
    : document_(&document),
      loader_(document),
      optional_content_(document),
      interpreter_(document, loader_, optional_content_) {}

PageContent TextReader::interpret(std::uint32_t number) {
  if (number == 0 || number > document_->pages().size()) {
    throw std::out_of_range("page number outside the document");
  }
  return interpreter_.run(document_->pages()[number - 1]);
}

PageText TextReader::read(std::uint32_t number) {
  PageContent content = interpret(number);
  ActualText::apply(content.glyphs, content.spans);
  HiddenText::classify(content);

  PageText out;
  out.page = number;
  std::vector<std::size_t> visible;
  std::vector<std::size_t> hidden;
  for (const GlyphRecord& glyph : content.glyphs) {
    (glyph.hidden == 0 ? visible : hidden).push_back(glyph.draw);
    ++out.rungs[static_cast<std::size_t>(glyph.rung)];
    if (glyph.rung == fonts::DecodingRung::unreadable && glyph.font != nullptr) {
      ++out.unreadable[glyph.font->name()];
    }
    for (std::size_t c = 0; c < hidden_classes.size(); ++c) {
      out.hidden_counts[c] += glyph.has(hidden_classes[c]) ? 1U : 0U;
    }
    out.hidden += glyph.hidden != 0 ? 1U : 0U;
  }

  // The structure order, when every MCID it orders on this page is drawn by
  // the page's own content.
  std::map<std::int64_t, std::size_t> rank;
  if (structure_ == nullptr) {
    structure_ = std::make_unique<StructureOrder>(*document_);
  }
  if (structure_->present()) {
    const std::vector<std::int64_t>& mcids =
        structure_->mcids(document_->pages()[number - 1].reference.number);
    std::set<std::int64_t> drawn;
    for (const GlyphRecord& glyph : content.glyphs) {
      if (glyph.stream == 0 && glyph.mcid >= 0) {
        drawn.insert(glyph.mcid);
      }
    }
    const bool usable = !mcids.empty() && std::ranges::all_of(mcids, [&](std::int64_t mcid) {
      return drawn.contains(mcid);
    });
    if (usable) {
      for (std::size_t i = 0; i < mcids.size(); ++i) {
        rank.emplace(mcids[i], i);
      }
      out.structure_order = true;
    }
  }

  std::vector<Rect> rules;
  for (const FigureRecord& figure : content.figures) {
    for (const FigureRecord::Paint& paint : figure.paints) {
      if (!paint.box.is_empty() && std::min(paint.box.width(), paint.box.height()) <= 2.0) {
        rules.push_back(paint.box);
      }
    }
  }
  // Pages of very many thin shapes (a drawing) are laid out without them.
  constexpr std::size_t max_rules = 5000;
  if (rules.size() > max_rules) {
    rules.clear();
  }
  TextLayout::Result shown = TextLayout::run(content.glyphs, visible, 0, rank, rules);
  TextLayout::Result kept = TextLayout::run(content.glyphs, hidden, visible.size());
  out.order = std::move(shown.order);
  out.visible = out.order.size();
  out.order.insert(out.order.end(), kept.order.begin(), kept.order.end());
  out.blocks = std::move(shown.blocks);
  out.hidden_blocks = std::move(kept.blocks);

  bool damaged_reported = false;
  for (const syntax::Document::Warning& warning : document_->warnings()) {
    if (warning.page == number) {
      out.warnings.push_back({warning.code, warning.message});
      damaged_reported = damaged_reported || warning.code == "damaged_stream";
    }
  }
  const std::string page = "page " + std::to_string(number);
  if (content.damaged && !damaged_reported) {
    std::string message = page + " holds a content stream read only up to a fault";
    for (const std::string& fault : content.faults) {
      message += "; " + fault;
    }
    out.warnings.push_back({"damaged_stream", message});
  }
  if (const std::uint64_t count = out.unreadable_count(); count > 0) {
    std::string fonts;
    for (const auto& [name, codes] : out.unreadable) {
      fonts += (fonts.empty() ? "" : ", ") + name + " (" + std::to_string(codes) + ")";
    }
    out.warnings.push_back({"unreadable_text", page + " holds " + count_of(count, "code", "codes") +
                                                   " no decoding rung could read, in the fonts " +
                                                   fonts + "; they are emitted as no text"});
  }
  if (out.hidden > 0) {
    std::string classes;
    for (std::size_t c = 0; c < hidden_classes.size(); ++c) {
      if (out.hidden_counts[c] > 0) {
        classes += (classes.empty() ? "" : ", ") +
                   std::string(hidden_class_name(hidden_classes[c])) + " " +
                   std::to_string(out.hidden_counts[c]);
      }
    }
    out.warnings.push_back({"hidden_text", page + " holds " +
                                               count_of(out.hidden, "glyph", "glyphs") +
                                               " a viewer does not show (" + classes +
                                               "); they are extracted after the visible text"});
  }
  for (const std::string& name : content.estimated_widths) {
    out.warnings.push_back(
        {"font_not_embedded", page + " uses the font " + name +
                                  ", which has no program, no widths and no standard metrics; "
                                  "its widths are estimated"});
  }
  out.content = std::move(content);
  return out;
}

}  // namespace pdfkit::text
