// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The page text as units, the folded haystacks (one per folding, built on
// first use) and the search. A found range of folded text maps back to its
// units and from them to the glyph positions it covers; the occurrence runs
// from the first of those glyphs to the last, so the glyphs between them in
// reading order (a space glyph, an unreadable code, a joined hyphen) go with
// it.

#include "redact/match_finder.hpp"

#include "platform/utf8_sequence.hpp"
#include "text/hidden_class.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <utility>

namespace pdfkit::redact {

namespace {

using TextFolding = platform::unicode::Folding;

constexpr std::size_t none = std::numeric_limits<std::size_t>::max();

[[nodiscard]] bool hyphen(char32_t value) noexcept {
  return value == U'-' || value == 0xAD;
}

[[nodiscard]] unsigned key(const MatchFinder::Folding& folding) noexcept {
  return (folding.text.case_fold ? 1U : 0U) | (folding.text.diacritics ? 2U : 0U) |
         (folding.text.whitespace ? 4U : 0U) | (folding.hyphenation ? 8U : 0U);
}

}  // namespace

MatchFinder::MatchFinder(const text::PageText& page, std::set<std::string> left_out)
    : page_(&page), left_out_(std::move(left_out)) {
  build(false, plain_);
}

void MatchFinder::build(bool hyphenation, std::vector<Unit>& units) const {
  const auto duplicate_only = text::hidden_bit(text::HiddenClass::duplicate);
  const auto separator = [&units](std::u32string_view text) {
    for (const char32_t value : text) {
      units.push_back({value, none, true});
    }
  };
  bool any = false;
  bool joined = false;
  for (const std::vector<text::TextLayout::Block>* blocks :
       {&page_->blocks, &page_->hidden_blocks}) {
    for (const text::TextLayout::Block& block : *blocks) {
      bool block_has = false;
      for (const text::TextLayout::Line& line : block.lines) {
        bool line_has = false;
        for (const text::TextLayout::Word& word : line.words) {
          if (word.text.empty() || word.hidden == duplicate_only) {
            continue;
          }
          if (!joined) {
            if (line_has) {
              separator(U" ");
            } else if (block_has) {
              separator(U"\n");
            } else if (any) {
              separator(U"\n\n");
            }
          }
          joined = false;
          for (std::size_t position = word.first;
               position < word.last && position < page_->order.size(); ++position) {
            const std::string& glyph = page_->content.glyphs[page_->order[position]].text;
            std::size_t at = 0;
            while (at < glyph.size()) {
              const platform::Utf8Sequence sequence = platform::next_utf8_sequence(glyph, at);
              units.push_back({sequence.valid ? sequence.code_point : U'�', position, false});
              at += std::max<std::size_t>(sequence.length, 1);
            }
          }
          if (hyphenation && word.hyphenated && !units.empty() && !units.back().separator &&
              hyphen(units.back().code_point)) {
            units.pop_back();
            joined = true;
          }
          line_has = true;
          block_has = true;
          any = true;
        }
      }
    }
  }
}

const MatchFinder::Haystack& MatchFinder::haystack(const Folding& folding) {
  const unsigned index = key(folding);
  if (const auto found = folded_.find(index); found != folded_.end()) {
    return found->second;
  }
  if (folding.hyphenation && joined_.empty()) {
    build(true, joined_);
  }
  const std::vector<Unit>& units = folding.hyphenation ? joined_ : plain_;
  std::u32string text;
  text.reserve(units.size());
  for (const Unit& unit : units) {
    text.push_back(unit.code_point);
  }
  TextFolding::Mapped mapped = TextFolding::fold(text, folding.text);
  Haystack built{std::move(mapped.text), std::move(mapped.source)};
  return folded_.emplace(index, std::move(built)).first->second;
}

namespace {

// The folded needle of a search, its white space trimmed when white space
// folds.
[[nodiscard]] std::u32string folded_needle(const MatchFinder::Match& match) {
  std::u32string needle =
      TextFolding::fold(TextFolding::decode(match.search), match.folding.text).text;
  if (match.folding.text.whitespace) {
    while (!needle.empty() && needle.front() == U' ') {
      needle.erase(needle.begin());
    }
    while (!needle.empty() && needle.back() == U' ') {
      needle.pop_back();
    }
  }
  return needle;
}

}  // namespace

std::vector<std::pair<std::size_t, std::size_t>> MatchFinder::ranges_in(std::u32string_view text,
                                                                        const Match& match) {
  std::vector<std::pair<std::size_t, std::size_t>> out;
  const std::u32string needle = folded_needle(match);
  if (needle.empty() || text.empty()) {
    return out;
  }
  const TextFolding::Mapped folded = TextFolding::fold(text, match.folding.text);
  std::size_t from = 0;
  while (from < folded.text.size()) {
    const std::size_t at = folded.text.find(needle, from);
    if (at == std::u32string::npos) {
      break;
    }
    const std::size_t end = at + needle.size();
    out.emplace_back(folded.source[at], folded.source[end - 1] + 1);
    from = end;
  }
  return out;
}

std::vector<MatchFinder::Occurrence> MatchFinder::find(const Match& match, std::size_t index) {
  std::vector<Occurrence> found;
  const std::u32string needle = folded_needle(match);
  if (needle.empty()) {
    return found;
  }
  const Haystack& hay = haystack(match.folding);
  const std::vector<Unit>& units = match.folding.hyphenation ? joined_ : plain_;
  std::size_t from = 0;
  while (from < hay.text.size()) {
    const std::size_t at = hay.text.find(needle, from);
    if (at == std::u32string::npos) {
      break;
    }
    const std::size_t end = at + needle.size();
    std::size_t first = none;
    std::size_t last = 0;
    for (std::size_t u = hay.unit[at]; u <= hay.unit[end - 1] && u < units.size(); ++u) {
      if (!units[u].separator) {
        first = std::min(first, units[u].position);
        last = std::max(last, units[u].position + 1);
      }
    }
    if (first != none) {
      found.push_back({index, page_->page, first, last, match.placeholder});
    }
    from = end;
  }
  return found;
}

void MatchFinder::index_positions() {
  const std::vector<text::GlyphRecord>& glyphs = page_->content.glyphs;
  if (!position_.empty() || glyphs.empty()) {
    return;
  }
  position_.assign(glyphs.size(), none);
  for (std::size_t p = 0; p < page_->order.size(); ++p) {
    if (page_->order[p] < position_.size()) {
      position_[page_->order[p]] = p;
    }
  }
}

namespace {

// A word gap ends a word in the other orders where the next glyph starts more
// than this share of the font size past the end of the one before: the
// dataset measured the gaps inside words within 0.07 of the size and those
// between positioned words from 0.15 up (docs/DESIGN.md section 7).
constexpr double word_gap = 0.15;
// In the drawing order the next glyph continues the word when it starts at
// most this share of the size back (a kern, the TeX logo's E and A) and its
// origin lies within this share of the size of the baseline before (a
// lowered or raised letter), so a glyph drawn elsewhere starts a word.
constexpr double back_step = 0.6;
constexpr double baseline_step = 0.5;
// The lines of a row have baselines within this share of the size, the
// layout's own rule for one line.
constexpr double row_baseline = 0.2;

[[nodiscard]] double rounded(double value) {
  return std::round(value * 100.0) / 100.0;
}

// A direction in device space: a point's distance along it and across it.
class Axis {
 public:
  explicit Axis(double degrees)
      : dx_(std::cos(degrees * std::numbers::pi / 180.0)),
        dy_(std::sin(degrees * std::numbers::pi / 180.0)) {}

  [[nodiscard]] double along(const text::Matrix::Point& point) const {
    return (point.x * dx_) + (point.y * dy_);
  }
  [[nodiscard]] double across(const text::Matrix::Point& point) const {
    return (point.y * dx_) - (point.x * dy_);
  }

 private:
  double dx_;
  double dy_;
};

[[nodiscard]] double size_of(const text::GlyphRecord& a, const text::GlyphRecord& b) {
  return std::max(std::abs(a.size), std::abs(b.size));
}

// Whether b, drawn right after a, continues a's word; axis is a's direction.
[[nodiscard]] bool drawn_on(const text::GlyphRecord& a, const text::GlyphRecord& b,
                            const Axis& axis) {
  if (rounded(a.angle) != rounded(b.angle)) {
    return false;
  }
  const double size = size_of(a, b);
  const double gap = axis.along(b.origin) - axis.along(a.advance_end);
  const double drop = std::abs(axis.across(b.origin) - axis.across(a.origin));
  return drop <= baseline_step * size && gap <= word_gap * size && gap >= -back_step * size;
}

// A line of the page with its direction, its baseline across that direction
// and its first glyph's size.
struct LineAt {
  const text::TextLayout::Line* line = nullptr;
  double angle = 0.0;
  double baseline = 0.0;
  double size = 0.0;
};

// A glyph's extent along a row's baseline, from its origin to its advance
// end whichever way it runs.
struct Extent {
  std::size_t draw = 0;
  double low = 0.0;
  double high = 0.0;
};

}  // namespace

void MatchFinder::add_step(std::vector<Step>& steps, std::size_t draw, bool& gap) const {
  if (page_->content.glyphs[draw].text.empty()) {
    return;
  }
  steps.push_back({draw, gap && !steps.empty()});
  gap = false;
}

bool MatchFinder::in_orders(std::size_t draw) const {
  if (draw >= page_->content.glyphs.size() || position_[draw] == none) {
    return false;
  }
  const text::GlyphRecord& record = page_->content.glyphs[draw];
  return !record.duplicate_of &&
         (record.font == nullptr || !left_out_.contains(record.font->name()));
}

void MatchFinder::build_orders() {
  if (!orders_.empty()) {
    return;
  }
  index_positions();
  const std::vector<text::GlyphRecord>& glyphs = page_->content.glyphs;
  orders_.resize(3);

  // The drawing order.
  std::optional<std::size_t> before;
  bool gap = false;
  double axis_angle = 0.0;
  Axis drawn_axis(axis_angle);
  for (std::size_t draw = 0; draw < glyphs.size(); ++draw) {
    if (!in_orders(draw)) {
      continue;
    }
    if (before) {
      // The direction changes seldom, so its axis is computed once per run.
      if (rounded(glyphs[*before].angle) != axis_angle) {
        axis_angle = rounded(glyphs[*before].angle);
        drawn_axis = Axis(axis_angle);
      }
      if (!drawn_on(glyphs[*before], glyphs[draw], drawn_axis)) {
        gap = true;
      }
    }
    add_step(orders_[0], draw, gap);
    before = draw;
  }

  // The rows: every line of the page, the hidden ones too, grouped by
  // direction and baseline, in the reading order of their first lines.
  std::vector<LineAt> lines;
  for (const std::vector<text::TextLayout::Block>* blocks :
       {&page_->blocks, &page_->hidden_blocks}) {
    for (const text::TextLayout::Block& block : *blocks) {
      for (const text::TextLayout::Line& line : block.lines) {
        for (std::size_t p = line.first; p < line.last && p < page_->order.size(); ++p) {
          const text::GlyphRecord& record = glyphs[page_->order[p]];
          if (record.size != 0.0) {
            const double angle = rounded(line.angle);
            lines.push_back(
                {&line, angle, rounded(Axis(angle).across(record.origin)), std::abs(record.size)});
            break;
          }
        }
      }
    }
  }
  std::ranges::stable_sort(lines, [](const LineAt& a, const LineAt& b) {
    if (a.angle != b.angle) {
      return a.angle < b.angle;
    }
    if (a.baseline != b.baseline) {
      return a.baseline < b.baseline;
    }
    return a.line->first < b.line->first;
  });
  // Each row with the reading-order position of its first line.
  std::vector<std::pair<std::size_t, std::vector<const LineAt*>>> rows;
  for (const LineAt& line : lines) {
    if (rows.empty() || rows.back().second.front()->angle != line.angle ||
        line.baseline - rows.back().second.front()->baseline >
            row_baseline * rows.back().second.front()->size) {
      rows.emplace_back(line.line->first, std::vector<const LineAt*>{});
    }
    rows.back().first = std::min(rows.back().first, line.line->first);
    rows.back().second.push_back(&line);
  }
  std::ranges::stable_sort(rows, [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [first, row] : rows) {
    const Axis axis(row.front()->angle);
    std::vector<Extent> extents;
    for (const LineAt* line : row) {
      for (std::size_t p = line->line->first; p < line->line->last && p < page_->order.size();
           ++p) {
        const std::size_t draw = page_->order[p];
        if (in_orders(draw)) {
          const double from = axis.along(glyphs[draw].origin);
          const double to = axis.along(glyphs[draw].advance_end);
          extents.push_back({draw, rounded(std::min(from, to)), rounded(std::max(from, to))});
        }
      }
    }
    std::ranges::stable_sort(extents, [](const Extent& a, const Extent& b) {
      return a.low != b.low ? a.low < b.low : a.draw < b.draw;
    });
    // Forwards: a glyph starting past the reach of every glyph before it by
    // more than a word gap starts a word; backwards likewise from the far end.
    gap = true;
    double reach = 0.0;
    for (std::size_t i = 0; i < extents.size(); ++i) {
      const Extent& extent = extents[i];
      if (i > 0 && extent.low - reach >
                       word_gap * size_of(glyphs[extents[i - 1].draw], glyphs[extent.draw])) {
        gap = true;
      }
      reach = i == 0 ? extent.high : std::max(reach, extent.high);
      add_step(orders_[1], extent.draw, gap);
    }
    gap = true;
    double low = 0.0;
    for (std::size_t i = extents.size(); i > 0; --i) {
      const Extent& extent = extents[i - 1];
      if (i < extents.size() &&
          low - extent.high > word_gap * size_of(glyphs[extents[i].draw], glyphs[extent.draw])) {
        gap = true;
      }
      low = i == extents.size() ? extent.low : std::min(low, extent.low);
      add_step(orders_[2], extent.draw, gap);
    }
  }
}

const std::vector<MatchFinder::OrderHaystack>& MatchFinder::order_haystacks(
    const Folding& folding) {
  // The hyphenation join plays no part in these orders.
  const unsigned index = key(folding) & 7U;
  if (const auto found = order_folded_.find(index); found != order_folded_.end()) {
    return found->second;
  }
  build_orders();
  // Each glyph's text folded once: the folding works code point by code
  // point, so a glyph folds alike in every order.
  const std::vector<text::GlyphRecord>& glyphs = page_->content.glyphs;
  std::u32string folded;
  std::vector<std::size_t> offset(glyphs.size() + 1, 0);
  for (std::size_t draw = 0; draw < glyphs.size(); ++draw) {
    offset[draw] = folded.size();
    if (!in_orders(draw)) {
      continue;
    }
    const std::string& text = glyphs[draw].text;
    std::size_t at = 0;
    while (at < text.size()) {
      const platform::Utf8Sequence sequence = platform::next_utf8_sequence(text, at);
      const char32_t value = sequence.valid ? sequence.code_point : U'�';
      if (folding.text.whitespace && TextFolding::is_white_space(value)) {
        folded.push_back(U' ');
      } else {
        TextFolding::fold_code_point(value, folding.text, folded);
      }
      at += std::max<std::size_t>(sequence.length, 1);
    }
  }
  offset[glyphs.size()] = folded.size();
  const bool whitespace = folding.text.whitespace;
  std::vector<OrderHaystack> built;
  for (const std::vector<Step>& steps : orders_) {
    OrderHaystack& hay = built.emplace_back();
    bool gap = false;
    for (std::size_t s = 0; s < steps.size(); ++s) {
      gap = gap || steps[s].gap;
      if (gap && !whitespace && !hay.text.empty()) {
        hay.text.push_back(U' ');
        hay.step.push_back(s);
        hay.gap.push_back(false);
        gap = false;
      }
      for (std::size_t i = offset[steps[s].draw]; i < offset[steps[s].draw + 1]; ++i) {
        if (whitespace && folded[i] == U' ') {
          gap = true;
          continue;
        }
        hay.text.push_back(folded[i]);
        hay.step.push_back(s);
        hay.gap.push_back(gap);
        gap = false;
      }
    }
  }
  return order_folded_.emplace(index, std::move(built)).first->second;
}

std::vector<MatchFinder::Occurrence> MatchFinder::find_orders(const Match& match,
                                                              std::size_t index) {
  return find_orders(match, index, find(match, index));
}

std::vector<MatchFinder::Occurrence> MatchFinder::find_orders(const Match& match, std::size_t index,
                                                              std::span<const Occurrence> known) {
  std::vector<Occurrence> found;
  // The needle as the haystacks hold it, and the characters a space of it
  // precedes.
  std::u32string needle;
  std::vector<bool> starts;
  bool space = false;
  for (const char32_t value : folded_needle(match)) {
    if (match.folding.text.whitespace && value == U' ') {
      space = true;
      continue;
    }
    needle.push_back(value);
    starts.push_back(space);
    space = false;
  }
  if (needle.empty()) {
    return found;
  }
  std::vector<bool> covered(page_->order.size(), false);
  for (const Occurrence& occurrence : known) {
    for (std::size_t p = occurrence.first; p < occurrence.last && p < covered.size(); ++p) {
      covered[p] = true;
    }
  }
  const std::vector<OrderHaystack>& hays = order_haystacks(match.folding);
  for (std::size_t order = 0; order < hays.size(); ++order) {
    const OrderHaystack& hay = hays[order];
    const std::vector<Step>& steps = orders_[order];
    std::size_t from = 0;
    while (from < hay.text.size()) {
      const std::size_t at = hay.text.find(needle, from);
      if (at == std::u32string::npos) {
        break;
      }
      bool whole = true;
      for (std::size_t k = 1; k < needle.size() && whole; ++k) {
        whole = !hay.gap[at + k] || starts[k];
      }
      if (!whole) {
        from = at + 1;
        continue;
      }
      const std::size_t end = at + needle.size();
      std::vector<std::size_t> positions;
      for (std::size_t s = hay.step[at]; s <= hay.step[end - 1] && s < steps.size(); ++s) {
        const std::size_t p = position_[steps[s].draw];
        if (!covered[p]) {
          positions.push_back(p);
        }
      }
      std::ranges::sort(positions);
      const auto repeated = std::ranges::unique(positions);
      positions.erase(repeated.begin(), repeated.end());
      for (std::size_t i = 0; i < positions.size();) {
        std::size_t j = i + 1;
        while (j < positions.size() && positions[j] == positions[j - 1] + 1) {
          ++j;
        }
        found.push_back({index, page_->page, positions[i], positions[j - 1] + 1,
                         i == 0 ? match.placeholder : std::string()});
        i = j;
      }
      for (const std::size_t p : positions) {
        covered[p] = true;
      }
      from = end;
    }
  }
  return found;
}

std::string MatchFinder::lower_ascii(std::string bytes) {
  for (char& value : bytes) {
    if (value >= 'A' && value <= 'Z') {
      value = static_cast<char>(value - 'A' + 'a');
    }
  }
  return bytes;
}

std::vector<std::string> MatchFinder::byte_forms(const Match& match) {
  std::u32string text = TextFolding::decode(match.search);
  if (match.folding.text.whitespace) {
    std::u32string collapsed;
    for (const char32_t value : text) {
      if (TextFolding::is_white_space(value)) {
        if (!collapsed.empty() && collapsed.back() != U' ') {
          collapsed.push_back(U' ');
        }
      } else {
        collapsed.push_back(value);
      }
    }
    while (!collapsed.empty() && collapsed.back() == U' ') {
      collapsed.pop_back();
    }
    text = std::move(collapsed);
  }
  std::vector<std::string> out;
  if (text.empty()) {
    return out;
  }
  out.push_back(TextFolding::encode(text));
  std::string latin;
  bool fits = true;
  std::string utf16;
  const auto unit = [&utf16](char32_t value) {
    utf16.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    utf16.push_back(static_cast<char>(value & 0xFFU));
  };
  for (const char32_t value : text) {
    fits = fits && value < 0x100;
    latin.push_back(static_cast<char>(value & 0xFFU));
    if (value >= 0x10000) {
      const char32_t offset = value - 0x10000;
      unit(0xD800 + (offset >> 10U));
      unit(0xDC00 + (offset & 0x3FFU));
    } else {
      unit(value);
    }
  }
  if (fits && latin != out.front()) {
    out.push_back(latin);
  }
  out.push_back(utf16);
  if (match.folding.text.case_fold) {
    for (std::string& form : out) {
      form = lower_ascii(std::move(form));
    }
  }
  return out;
}

std::vector<MatchFinder::Occurrence> MatchFinder::find_codes(const Match& match,
                                                             std::size_t index) {
  std::vector<Occurrence> found;
  const std::vector<std::string> forms = byte_forms(match);
  if (forms.empty()) {
    return found;
  }
  const std::vector<text::GlyphRecord>& glyphs = page_->content.glyphs;
  index_positions();
  if (operands_.empty()) {
    std::size_t start = 0;
    while (start < glyphs.size()) {
      std::size_t end = start + 1;
      while (end < glyphs.size() && glyphs[end].stream == glyphs[start].stream &&
             glyphs[end].operation == glyphs[start].operation &&
             glyphs[end].operand == glyphs[start].operand) {
        ++end;
      }
      Operand& operand = operands_.emplace_back();
      for (std::size_t g = start; g < end; ++g) {
        for (std::size_t i = glyphs[g].code_length; i > 0; --i) {
          operand.bytes.push_back(static_cast<char>((glyphs[g].code >> (8U * (i - 1))) & 0xFFU));
          operand.owner.push_back(g);
        }
      }
      operand.lowered = lower_ascii(operand.bytes);
      start = end;
    }
  }
  for (const Operand& operand : operands_) {
    const std::string& bytes = match.folding.text.case_fold ? operand.lowered : operand.bytes;
    const std::vector<std::size_t>& owner = operand.owner;
    for (const std::string& form : forms) {
      for (std::size_t at = bytes.find(form); at != std::string::npos;
           at = bytes.find(form, at + 1)) {
        std::vector<std::size_t> positions;
        for (std::size_t b = at; b < at + form.size(); ++b) {
          const std::size_t p = position_[owner[b]];
          if (p != none && (positions.empty() || positions.back() != p)) {
            positions.push_back(p);
          }
        }
        std::ranges::sort(positions);
        const auto repeated = std::ranges::unique(positions);
        positions.erase(repeated.begin(), repeated.end());
        for (std::size_t i = 0; i < positions.size();) {
          std::size_t j = i + 1;
          while (j < positions.size() && positions[j] == positions[j - 1] + 1) {
            ++j;
          }
          found.push_back({index, page_->page, positions[i], positions[j - 1] + 1,
                           i == 0 ? match.placeholder : std::string()});
          i = j;
        }
      }
    }
  }
  return found;
}

std::string MatchFinder::text(std::size_t first, std::size_t last) const {
  std::string out;
  std::string pending;
  bool started = false;
  for (const Unit& unit : plain_) {
    if (unit.separator) {
      if (started) {
        platform::append_utf8(pending, unit.code_point);
      }
      continue;
    }
    if (unit.position < first || unit.position >= last) {
      pending.clear();
      continue;
    }
    out += pending;
    pending.clear();
    platform::append_utf8(out, unit.code_point);
    started = true;
  }
  return out;
}

std::vector<MatchFinder::Occurrence> MatchFinder::merge(std::vector<Occurrence> occurrences) {
  std::ranges::stable_sort(occurrences, [](const Occurrence& a, const Occurrence& b) {
    return a.first != b.first ? a.first < b.first : a.last > b.last;
  });
  std::vector<Occurrence> merged;
  for (Occurrence& next : occurrences) {
    if (!merged.empty()) {
      Occurrence& current = merged.back();
      if (next.first <= current.last && next.placeholder == current.placeholder) {
        current.last = std::max(current.last, next.last);
        continue;
      }
      if (next.first < current.last) {
        next.first = current.last;
        if (next.first >= next.last) {
          continue;
        }
      }
    }
    merged.push_back(std::move(next));
  }
  return merged;
}

}  // namespace pdfkit::redact
