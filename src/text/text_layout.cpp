// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The layout pass: glyphs to raw lines by direction and baseline, raw lines to
// ordered glyphs and words, lines to Docstrum blocks, blocks to reading order
// by XY-cut in the page's dominant direction. Every geometric key goes
// through key() before it is compared.

#include "text/text_layout.hpp"

#include "fonts/pdf_font.hpp"
#include "platform/utf8_sequence.hpp"
#include "text/docstrum.hpp"
#include "text/xy_cut.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>

namespace pdfkit::text {

namespace {

// Between two ideographic, kana, hangul or full-width glyphs, which no space
// separates in the scripts that use them, a gap ends a word only from this
// many font sizes.
constexpr double ideographic_gap = 1.0;
constexpr double line_gap = 2.0;
constexpr double baseline_tolerance = 0.2;
// A glyph drawn this many font sizes before the one drawn just ahead of it on
// its baseline starts a new run of the content.
constexpr double run_backstep = 0.5;
// Two glyphs whose baselines lie more than this many font sizes apart are not
// one drawn over the other: a placeholder is drawn on the baseline of the
// text it replaces, a subscript or a superscript off it.
constexpr double overprint_baseline = 0.05;
// On a line whose words are marked by space glyphs, a gap between two
// text-showing operators drawn one after the other ends a word from this
// many font sizes:
// the narrowest gap the dataset measured between positioned words.
constexpr double repositioned_word_gap = 0.15;
// Two runs cross only when the smaller's size is at least this share of the
// larger's: a subscript or a superscript is set at 0.7 of its line or less,
// pdfdiff-0018's two lines at 500 and 660 pt (0.76).
constexpr double crossing_size_share = 0.75;
// The glyphs one baseline cluster may compare to find runs drawn across
// each other, so a page of many baselines in one cluster stays linear.
constexpr std::size_t crossing_budget = 1'000'000;
// Runs of at most this many glyphs take their place by position.
constexpr std::size_t short_run = 2;
// A Type 3 glyph whose box spans more than this many times its font size
// across the line takes the box's span as its size.
constexpr double type3_scale = 4.0;
// A painted shape this thin (in points) is a ruling.
constexpr double max_rule_thickness = 2.0;

[[nodiscard]] double key(double value) noexcept {
  return std::round(value * 100.0) / 100.0;
}

// The direction bucket of an angle: whole degrees in (-180, 180].
[[nodiscard]] int bucket_of(double angle) noexcept {
  int degrees = static_cast<int>(std::lround(angle));
  while (degrees <= -180) {
    degrees += 360;
  }
  while (degrees > 180) {
    degrees -= 360;
  }
  return degrees;
}

struct Frame {
  double dx = 1.0;
  double dy = 0.0;

  explicit Frame(int degrees) {
    const double radians = static_cast<double>(degrees) * std::numbers::pi / 180.0;
    dx = std::cos(radians);
    dy = std::sin(radians);
    // Exact axes for the common directions, so keys do not drift by 1e-17.
    if (degrees % 90 == 0) {
      dx = std::round(dx);
      dy = std::round(dy);
    }
  }
  [[nodiscard]] double along(double x, double y) const noexcept { return (x * dx) + (y * dy); }
  [[nodiscard]] double across(double x, double y) const noexcept { return (y * dx) - (x * dy); }
};

// One glyph in the frame of its direction.
struct Item {
  std::size_t draw = 0;
  int bucket = 0;
  double low = 0.0;
  double high = 0.0;
  double origin = 0.0;
  // Where the glyph's width ends along the line, its character and word
  // spacing left out (the far side of its box from the origin).
  double width_end = 0.0;
  double across = 0.0;
  double across_low = 0.0;
  double across_high = 0.0;
  double size = 1.0;
};

// A line under construction: its glyphs in reading order and its words as
// [first, last) ranges of that list.
struct RawLine {
  int bucket = 0;
  std::vector<std::size_t> items;
  std::vector<std::pair<std::size_t, std::size_t>> words;
  bool right_to_left = false;
  double along_low = std::numeric_limits<double>::infinity();
  double along_high = -std::numeric_limits<double>::infinity();
  double across_low = std::numeric_limits<double>::infinity();
  double across_high = -std::numeric_limits<double>::infinity();
  double size = 0.0;
};

[[nodiscard]] char32_t first_code_point(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  const platform::Utf8Sequence unit = platform::next_utf8_sequence(text, 0);
  return unit.valid ? unit.code_point : 0;
}

[[nodiscard]] char32_t last_code_point(const std::string& text) {
  std::size_t at = 0;
  char32_t last = 0;
  while (at < text.size()) {
    const platform::Utf8Sequence unit = platform::next_utf8_sequence(text, at);
    last = unit.valid ? unit.code_point : 0;
    at += unit.length;
  }
  return last;
}

[[nodiscard]] bool right_to_left_letter(char32_t c) noexcept {
  return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) ||
         (c >= 0xFE70 && c <= 0xFEFF);
}

[[nodiscard]] bool left_to_right_letter(char32_t c) noexcept {
  return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
         (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || (c >= 0x370 && c <= 0x52F) ||
         (c >= 0x1100 && !right_to_left_letter(c) && c < 0xFFF0);
}

// A lower-case letter of Latin, Latin-1, Greek or Cyrillic.
[[nodiscard]] bool lower_case(char32_t c) noexcept {
  return (c >= U'a' && c <= U'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) ||
         (c >= 0x3AC && c <= 0x3CE) || (c >= 0x430 && c <= 0x45F);
}

// A character of the scripts written without spaces between words: CJK
// ideographs and their extensions, kana, hangul syllables, CJK punctuation
// and the full-width forms (the Unicode blocks, recalled).
[[nodiscard]] bool ideographic(char32_t c) noexcept {
  return (c >= 0x3000 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x9FFF) ||
         (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
         (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x2FA1F);
}

[[nodiscard]] bool letter(char32_t c) noexcept {
  return left_to_right_letter(c) || right_to_left_letter(c);
}

// The name of a glyph's font without a subset tag (six upper-case letters and
// a plus sign, ISO 32000-1 section 9.6.4, recalled) and without a suffix of a
// plus sign and digits (a further encoding of one face, "WarnockPro-Regular+20",
// as the producers of the dataset's files write it), so one face stated as
// several font objects, cut into subsets or encoded twice is one font; empty
// for a glyph without one.
[[nodiscard]] std::string_view font_name(const GlyphRecord& glyph) noexcept {
  if (glyph.font == nullptr) {
    return {};
  }
  std::string_view name(glyph.font->name());
  constexpr std::size_t tag = 6;
  if (name.size() > tag + 1 && name[tag] == '+' &&
      std::all_of(name.begin(), name.begin() + tag, [](char c) { return c >= 'A' && c <= 'Z'; })) {
    name.remove_prefix(tag + 1);
  }
  const std::size_t plus = name.rfind('+');
  if (plus != std::string_view::npos && plus > 0 && plus + 1 < name.size() &&
      std::all_of(name.begin() + static_cast<std::ptrdiff_t>(plus) + 1, name.end(),
                  [](char c) { return c >= '0' && c <= '9'; })) {
    name.remove_suffix(name.size() - plus);
  }
  return name;
}

// An accent a producer may draw over its letter on its own: a combining mark
// (the combining diacritical marks and their extensions, the half marks and
// the marks for symbols), a spacing modifier letter, or a spacing accent of
// ASCII or Latin-1 (grave, circumflex, tilde, diaeresis, macron, acute,
// cedilla); the Unicode blocks, recalled.
[[nodiscard]] bool accent(char32_t c) noexcept {
  return (c >= 0x0300 && c <= 0x036F) || (c >= 0x1AB0 && c <= 0x1AFF) ||
         (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x20D0 && c <= 0x20FF) ||
         (c >= 0xFE20 && c <= 0xFE2F) || (c >= 0x02B0 && c <= 0x02FF) || c == U'`' || c == U'^' ||
         c == U'~' || c == 0xA8 || c == 0xAF || c == 0xB4 || c == 0xB8;
}

class Builder {
 public:
  Builder(const std::vector<GlyphRecord>& glyphs, const std::vector<std::size_t>& members,
          const std::vector<Rect>& rules)
      : glyphs_(glyphs) {
    // A horizontal or vertical ruling separates lines of the directions it
    // crosses: a horizontal one the left-to-right lines, a vertical one the
    // top-to-bottom ones.
    for (const Rect& rule : rules) {
      if (rule.height() <= max_rule_thickness && rule.width() > rule.height()) {
        rules_.push_back({0, rule.left, rule.right, (rule.bottom + rule.top) / 2.0});
      } else if (rule.width() <= max_rule_thickness && rule.height() > rule.width()) {
        rules_.push_back({-90, -rule.top, -rule.bottom, (rule.left + rule.right) / 2.0});
      }
    }
    items_.reserve(members.size());
    for (const std::size_t draw : members) {
      items_.push_back(item_of(draw));
    }
  }

  TextLayout::Result run(std::size_t offset, const std::map<std::int64_t, std::size_t>& rank) {
    build_lines();
    for (RawLine& line : lines_) {
      order_line(line);
      split_words(line);
    }
    std::vector<Docstrum::Line> geometry;
    geometry.reserve(lines_.size());
    for (const RawLine& line : lines_) {
      geometry.push_back({line.bucket, key(line.along_low), key(line.along_high),
                          key(line.across_low), key(line.across_high), line.size});
    }
    const std::vector<std::vector<std::size_t>> groups = Docstrum::blocks(geometry, rules_);
    const std::vector<std::size_t> block_order = order_blocks(groups, rank);
    TextLayout::Result result;
    for (const std::size_t group : block_order) {
      result.blocks.push_back(emit_block(groups[group], offset, result.order));
    }
    for (std::size_t b = 0; b < result.blocks.size(); ++b) {
      std::vector<TextLayout::Line>& lines = result.blocks[b].lines;
      for (std::size_t l = 0; l < lines.size(); ++l) {
        if (l + 1 < lines.size()) {
          TextLayout::join_hyphen(lines[l], lines[l + 1]);
        } else if (b + 1 < result.blocks.size() && !result.blocks[b + 1].lines.empty()) {
          TextLayout::join_hyphen(lines[l], result.blocks[b + 1].lines.front());
        }
      }
    }
    return result;
  }

 private:
  [[nodiscard]] Item item_of(std::size_t draw) const {
    const GlyphRecord& glyph = glyphs_[draw];
    Item item;
    item.draw = draw;
    item.bucket = bucket_of(glyph.angle);
    const Frame frame(item.bucket);
    const double start = frame.along(glyph.origin.x, glyph.origin.y);
    const double end = frame.along(glyph.advance_end.x, glyph.advance_end.y);
    item.origin = key(start);
    item.low = key(std::min(start, end));
    item.high = key(std::max(start, end));
    const Matrix::Point baseline = (glyph.text_matrix * glyph.ctm).apply(0.0, 0.0);
    item.across = key(frame.across(baseline.x, baseline.y));
    item.size = glyph.size > 0.01 ? glyph.size : std::max(glyph.box.height(), 1.0);
    item.width_end = item.high;
    if (glyph.box.is_empty()) {
      const double across = frame.across(glyph.origin.x, glyph.origin.y);
      item.across_low = item.across_high = key(across);
    } else {
      const Rect& box = glyph.box;
      const double corners[4] = {frame.across(box.left, box.bottom),
                                 frame.across(box.right, box.bottom),
                                 frame.across(box.left, box.top), frame.across(box.right, box.top)};
      item.across_low = key(*std::ranges::min_element(corners));
      item.across_high = key(*std::ranges::max_element(corners));
      const double along[4] = {frame.along(box.left, box.bottom),
                               frame.along(box.right, box.bottom), frame.along(box.left, box.top),
                               frame.along(box.right, box.top)};
      item.width_end =
          key(start <= end ? *std::ranges::max_element(along) : *std::ranges::min_element(along));
      // A Type 3 font's em is whatever its /FontMatrix makes of its glyph
      // space, so its font size says little of how large it draws: a matrix
      // of 1 shown at 0.24 draws glyphs 72 units, 17 pt, tall (pdfjs-0047),
      // and every gap between its letters would be a line's end. Such a glyph
      // is as large as its box (the /FontBBox) spans.
      const double span = item.across_high - item.across_low;
      if (glyph.font != nullptr && glyph.font->kind() == fonts::PdfFont::Kind::type3 &&
          span > type3_scale * item.size) {
        item.size = span;
      }
    }
    if (glyph.is_space()) {
      take_back(item);
    }
    return item;
  }

  // A space that opens a run of the content and that the glyph drawn next
  // was moved back over, so far that the space lies wholly in its advance on
  // its baseline (pdfjs-0018: a Hebrew line drawn from its left end opens
  // with a space, and a TJ number sets the word's last letter 0.12 pt before
  // it), stands where that glyph starts: drawn first, it reads before the
  // glyph rather than inside its word. A space drawn straight after a glyph
  // keeps its place after that glyph.
  void take_back(Item& space) const {
    const std::size_t next = space.draw + 1;
    // The glyph drawn next was moved back by a TJ number.
    if (next >= glyphs_.size() || glyphs_[next].is_space() || glyphs_[next].adjustment <= 0.0) {
      return;
    }
    const Frame frame(space.bucket);
    // Where a glyph's advance starts and ends along the space's line, and
    // whether it stands on the space's baseline.
    const auto on_line = [&](const GlyphRecord& glyph) {
      const Matrix::Point baseline = (glyph.text_matrix * glyph.ctm).apply(0.0, 0.0);
      return bucket_of(glyph.angle) == space.bucket &&
             std::abs(key(frame.across(baseline.x, baseline.y)) - space.across) <=
                 overprint_baseline * space.size;
    };
    if (space.draw > 0) {
      const GlyphRecord& before = glyphs_[space.draw - 1];
      if (on_line(before) && std::abs(key(frame.along(before.advance_end.x, before.advance_end.y)) -
                                      space.low) <= TextLayout::overprint_gap * space.size) {
        return;
      }
    }
    const GlyphRecord& glyph = glyphs_[next];
    const double start = key(frame.along(glyph.origin.x, glyph.origin.y));
    const double end = key(frame.along(glyph.advance_end.x, glyph.advance_end.y));
    if (!on_line(glyph) || start >= end || space.low < start || space.high > end) {
      return;
    }
    space.origin = space.low = space.high = space.width_end = start;
  }

  void build_lines() {
    std::map<int, std::vector<std::size_t>> buckets;
    for (std::size_t i = 0; i < items_.size(); ++i) {
      buckets[items_[i].bucket].push_back(i);
    }
    for (auto& [bucket, members] : buckets) {
      std::ranges::stable_sort(members, [&](std::size_t a, std::size_t b) {
        return std::pair(items_[a].across, items_[a].draw) <
               std::pair(items_[b].across, items_[b].draw);
      });
      std::vector<std::vector<std::size_t>> clusters;
      double anchor = 0.0;
      double anchor_size = 0.0;
      for (const std::size_t i : members) {
        const Item& item = items_[i];
        const double limit = baseline_tolerance * std::max(anchor_size, item.size);
        if (clusters.empty() || std::abs(item.across - anchor) > limit) {
          clusters.emplace_back();
          anchor = item.across;
          anchor_size = item.size;
        }
        clusters.back().push_back(i);
      }
      std::vector<std::vector<std::size_t>> lines;
      for (std::vector<std::size_t>& cluster : clusters) {
        for (std::vector<std::size_t>& part : split_crossing(std::move(cluster))) {
          lines.push_back(std::move(part));
        }
      }
      for (std::vector<std::size_t>& cluster : lines) {
        const std::vector<std::size_t> sequence = visual_sequence(std::move(cluster));
        RawLine* line = nullptr;
        double reach = 0.0;
        for (const std::size_t i : sequence) {
          const Item& item = items_[i];
          if (line == nullptr || item.low - reach > line_gap * std::max(line->size, item.size)) {
            lines_.emplace_back();
            line = &lines_.back();
            line->bucket = bucket;
            reach = item.high;
          }
          line->items.push_back(i);
          reach = std::max(reach, item.high);
          line->along_low = std::min(line->along_low, item.low);
          line->along_high = std::max(line->along_high, item.high);
          line->across_low = std::min(line->across_low, item.across_low);
          line->across_high = std::max(line->across_high, item.across_high);
          line->size = std::max(line->size, item.size);
        }
      }
    }
  }

  // A baseline cluster cut where two of its baselines carry runs drawn
  // across each other: lines of very large glyphs whose baselines lie within
  // the tolerance of their size (pdfdiff-0018: "B 8" at 500 pt and "8B" at
  // 660 pt on baselines 80 pt apart, drawn over one another), or a string
  // drawn twice a little below itself. A run is a stretch of glyphs drawn
  // one after another on one baseline; two runs of at least two glyphs other
  // than spaces cross when their baselines lie more than overprint_baseline
  // of the size apart and at least half of each run's glyphs have their
  // middles in the advance of a glyph of the other. The baselines are walked
  // in order, and one starts a new line when a run of it crosses a run of a
  // baseline already in the line. Glyphs drawn over others on their own
  // baseline (a name struck out, a bold of two draws) stay in one line, and
  // so do a raised letter of a logo, which crosses a glyph or two of its line
  // rather than a run, and a run of subscripts or superscripts, smaller than
  // crossing_size_share of the run it crosses (dccc-0128's "m1 x m2"). The
  // comparisons of one cluster stop at crossing_budget, and the cluster then
  // stays one line.
  [[nodiscard]] std::vector<std::vector<std::size_t>> split_crossing(
      std::vector<std::size_t> cluster) const {
    // The baselines, each with its glyphs and its runs (glyphs other than
    // spaces, in position order).
    struct Baseline {
      double across = 0.0;
      double size = 0.0;
      std::vector<std::size_t> items;
      std::vector<std::vector<std::size_t>> runs;
    };
    std::vector<Baseline> baselines;
    std::ranges::stable_sort(cluster, [&](std::size_t a, std::size_t b) {
      return std::pair(items_[a].across, items_[a].draw) <
             std::pair(items_[b].across, items_[b].draw);
    });
    for (const std::size_t i : cluster) {
      const Item& item = items_[i];
      if (baselines.empty() || baselines.back().across != item.across) {
        baselines.push_back({item.across, 0.0, {}, {}});
      }
      Baseline& baseline = baselines.back();
      const bool follows =
          !baseline.items.empty() && items_[baseline.items.back()].draw + 1 == item.draw;
      baseline.items.push_back(i);
      baseline.size = std::max(baseline.size, item.size);
      if (!follows || baseline.runs.empty()) {
        baseline.runs.emplace_back();
      }
      if (!glyphs_[item.draw].is_space()) {
        baseline.runs.back().push_back(i);
      }
    }
    std::vector<std::vector<std::size_t>> out;
    if (baselines.size() < 2) {
      out.push_back(std::move(cluster));
      return out;
    }
    for (Baseline& baseline : baselines) {
      std::erase_if(baseline.runs,
                    [](const std::vector<std::size_t>& run) { return run.size() < 2; });
      for (std::vector<std::size_t>& run : baseline.runs) {
        std::ranges::stable_sort(run, [&](std::size_t a, std::size_t b) { return ahead(a, b); });
      }
    }
    // The glyphs of a run compared so far, against crossing_budget.
    std::size_t compared = 0;
    // Whether at least half of from's glyphs have their middles in an
    // advance of a glyph of onto.
    const auto mostly_over = [&](const std::vector<std::size_t>& from,
                                 const std::vector<std::size_t>& onto) {
      std::size_t count = 0;
      compared += from.size();
      if (compared > crossing_budget) {
        return false;
      }
      for (const std::size_t i : from) {
        const double middle = (items_[i].low + items_[i].high) / 2.0;
        auto at = std::ranges::upper_bound(onto, middle, {},
                                           [&](std::size_t j) { return items_[j].low; });
        // The glyphs of onto starting before the middle, nearest first; a
        // few reach every advance that can cover it.
        for (int look = 0; look < 4 && at != onto.begin(); ++look) {
          --at;
          if (items_[*at].low < middle && middle < items_[*at].high) {
            ++count;
            break;
          }
        }
      }
      return 2 * count >= from.size();
    };
    const auto crosses = [&](const Baseline& a, const Baseline& b) {
      if (std::abs(a.across - b.across) <= overprint_baseline * std::max(a.size, b.size)) {
        return false;
      }
      const auto size_of = [&](const std::vector<std::size_t>& run) {
        double size = 0.0;
        for (const std::size_t i : run) {
          size = std::max(size, items_[i].size);
        }
        return size;
      };
      return std::ranges::any_of(a.runs, [&](const std::vector<std::size_t>& run) {
        return std::ranges::any_of(b.runs, [&](const std::vector<std::size_t>& other) {
          const double small = std::min(size_of(run), size_of(other));
          const double large = std::max(size_of(run), size_of(other));
          return small >= crossing_size_share * large && mostly_over(run, other) &&
                 mostly_over(other, run);
        });
      });
    };
    std::vector<std::size_t> in_line;
    out.emplace_back();
    for (std::size_t b = 0; b < baselines.size(); ++b) {
      if (std::ranges::any_of(in_line, [&](std::size_t other) {
            return crosses(baselines[b], baselines[other]);
          })) {
        out.emplace_back();
        in_line.clear();
      }
      in_line.push_back(b);
      out.back().insert(out.back().end(), baselines[b].items.begin(), baselines[b].items.end());
    }
    return out;
  }

  // Whether item, in another font than before and on its baseline, starts
  // more than TextLayout::overprint_gap of the size before before ends: it
  // was placed over before rather than after it (a placeholder
  // running over the next word, a stamp over a line). A space paints nothing
  // and is drawn over nothing; an accent drawn over its letter is left to
  // its letter; a subscript or a superscript tucked
  // under its letter's italic overhang is off the baseline; and the same
  // text drawn again where before starts is a bold made of two faces.
  [[nodiscard]] bool drawn_over(const Item& before, const Item& item) const {
    const GlyphRecord& first = glyphs_[before.draw];
    const GlyphRecord& second = glyphs_[item.draw];
    if (font_name(first) == font_name(second) || first.is_space() || second.is_space() ||
        accent(first_code_point(first.text)) || accent(first_code_point(second.text))) {
      return false;
    }
    const double size = std::max(before.size, item.size);
    if (std::abs(item.across - before.across) > overprint_baseline * size) {
      return false;
    }
    if (first.text == second.text &&
        std::abs(item.low - before.low) < TextLayout::overprint_gap * size) {
      return false;
    }
    // Before ends where its advance or its width ends, whichever comes first:
    // character spacing a TJ number takes back (TeX's mathematics) does not
    // put the next glyph over it.
    const double end = std::min(before.high, before.width_end);
    return item.low < end - (TextLayout::overprint_gap * size);
  }

  // The glyphs of one baseline in visual order along it. The content's runs
  // (glyphs drawn one after another, each at or after the one before) are
  // placed in the order they were drawn, each cut where a glyph placed before
  // it stands in a gap between two of its glyphs (text drawn later into
  // several spaces left for it: a form's values, a redaction's
  // placeholders). A piece drawn over glyphs placed before it (one of its
  // glyphs' middle lies in a placed glyph's advance: a name overprinted with
  // X, a shadow offset, a placeholder running over the next word) is kept
  // whole, so it reads as a string of its own rather than as letters
  // interleaved with the other's: it takes its place where it starts when
  // that is in a gap of the placed glyphs, else follows them. Every other
  // piece stays whole too and takes its place where it starts, since no
  // placed glyph stands in its gaps: a placed glyph inside one of its
  // advances (a space set at a tab stop of another column) does not cut a
  // word. A run of one or two glyphs (an accent, a glyph drawn out of turn,
  // right-to-left text drawn in logical order) takes its place glyph by
  // glyph by position. A glyph in another font drawn over the one drawn just
  // ahead of it starts a run, so a string run over the next word is not
  // shuffled into it.
  [[nodiscard]] std::vector<std::size_t> visual_sequence(std::vector<std::size_t> cluster) const {
    std::ranges::stable_sort(cluster, {}, [&](std::size_t i) { return items_[i].draw; });
    std::vector<std::vector<std::size_t>> runs;
    for (const std::size_t i : cluster) {
      const Item& item = items_[i];
      if (runs.empty()) {
        runs.emplace_back();
      } else {
        const Item& before = items_[runs.back().back()];
        if (item.low < before.low - (run_backstep * std::max(before.size, item.size)) ||
            drawn_over(before, item)) {
          runs.emplace_back();
        }
      }
      runs.back().push_back(i);
    }
    const auto by_position = [&](std::size_t a, std::size_t b) {
      return ahead(a, b);
    };
    for (std::vector<std::size_t>& run : runs) {
      std::ranges::stable_sort(run, by_position);
    }
    std::vector<std::size_t> sequence;
    sequence.reserve(cluster.size());
    for (const std::vector<std::size_t>& run : runs) {
      if (run.size() <= short_run) {
        for (const std::size_t i : run) {
          sequence.insert(std::ranges::upper_bound(sequence, i, by_position), i);
        }
        continue;
      }
      for (const std::vector<std::size_t>& piece : pieces(sequence, run)) {
        const bool over =
            std::ranges::any_of(piece, [&](std::size_t i) { return covered(sequence, i); });
        const auto at = over && covered(sequence, piece.front())
                            ? sequence.end()
                            : std::ranges::upper_bound(sequence, piece.front(), by_position);
        sequence.insert(at, piece.begin(), piece.end());
      }
    }
    return sequence;
  }

  // run, in position order, cut between two of its glyphs where the first
  // glyph of sequence after the first of them by position has its middle in
  // the gap between them.
  [[nodiscard]] std::vector<std::vector<std::size_t>> pieces(
      const std::vector<std::size_t>& sequence, const std::vector<std::size_t>& run) const {
    std::vector<std::vector<std::size_t>> out(1);
    for (std::size_t k = 0; k < run.size(); ++k) {
      if (k > 0) {
        const Item& left = items_[run[k - 1]];
        const Item& right = items_[run[k]];
        const auto next = std::ranges::upper_bound(
            sequence, run[k - 1], [&](std::size_t a, std::size_t b) { return ahead(a, b); });
        if (next != sequence.end()) {
          const double middle = (items_[*next].low + items_[*next].high) / 2.0;
          if (left.high < middle && middle < right.low) {
            out.emplace_back();
          }
        }
      }
      out.back().push_back(run[k]);
    }
    return out;
  }

  // Whether glyph a goes before glyph b by position (where it starts, then
  // the draw order).
  [[nodiscard]] bool ahead(std::size_t a, std::size_t b) const {
    return std::pair(items_[a].low, items_[a].draw) < std::pair(items_[b].low, items_[b].draw);
  }

  // Whether glyph's middle lies in the advance of one of the two glyphs of
  // sequence either side of where it goes by position.
  [[nodiscard]] bool covered(const std::vector<std::size_t>& sequence, std::size_t glyph) const {
    const auto at = std::ranges::upper_bound(
        sequence, glyph, [&](std::size_t a, std::size_t b) { return ahead(a, b); });
    const double middle = (items_[glyph].low + items_[glyph].high) / 2.0;
    const auto covers = [&](std::size_t placed) {
      return items_[placed].low < middle && middle < items_[placed].high;
    };
    return (at != sequence.end() && covers(*at)) ||
           (at != sequence.begin() && covers(*std::prev(at)));
  }

  // The reading direction of each glyph's text: right to left, left to
  // right (letters and digits), or neutral.
  [[nodiscard]] int strength(std::size_t item) const {
    const std::string& text = glyphs_[items_[item].draw].text;
    // A glyph whose text holds a space and more after it stands for a
    // cluster a producer mapped onto it whole (pypdf-0019: the first glyph
    // of "habibi" maps to an Arabic word, a space and "h"); it is read with
    // the glyphs after it by what follows the space.
    std::size_t at = text.rfind(' ');
    at = at == std::string::npos || at + 1 == text.size() ? 0 : at + 1;
    while (at < text.size()) {
      const platform::Utf8Sequence unit = platform::next_utf8_sequence(text, at);
      if (unit.valid) {
        if (right_to_left_letter(unit.code_point)) {
          return -1;
        }
        if (left_to_right_letter(unit.code_point) ||
            (unit.code_point >= U'0' && unit.code_point <= U'9')) {
          return 1;
        }
      }
      at += unit.length;
    }
    return 0;
  }

  // Reverses each maximal run of glyphs of strength wanted (neutrals between
  // two of them included).
  void reverse_runs(std::vector<std::size_t>& items, int wanted) const {
    std::size_t k = 0;
    while (k < items.size()) {
      if (strength(items[k]) != wanted) {
        ++k;
        continue;
      }
      std::size_t last = k;
      for (std::size_t m = k + 1; m < items.size(); ++m) {
        const int value = strength(items[m]);
        if (value == wanted) {
          last = m;
        } else if (value != 0) {
          break;
        }
      }
      std::reverse(items.begin() + static_cast<std::ptrdiff_t>(k),
                   items.begin() + static_cast<std::ptrdiff_t>(last) + 1);
      k = last + 1;
    }
  }

  // Turns a line from visual order into reading order: a line whose strong
  // characters are mostly right-to-left reads from its far end, its
  // left-to-right runs (Latin words, numbers) kept left to right; a
  // left-to-right line reads its right-to-left runs from their far ends.
  // This is the reading the content's own drawing order gives when the
  // producer drew in logical order (docs/DESIGN.md chapter 7), found from
  // the positions so that a producer drawing in visual order reads the same.
  void order_line(RawLine& line) const {
    std::size_t rtl = 0;
    std::size_t ltr = 0;
    for (const std::size_t i : line.items) {
      const int value = strength(i);
      rtl += value < 0 ? 1U : 0U;
      ltr += value > 0 ? 1U : 0U;
    }
    line.right_to_left = rtl > ltr;
    if (rtl == 0) {
      return;
    }
    if (line.right_to_left) {
      std::ranges::reverse(line.items);
      reverse_runs(line.items, 1);
    } else {
      reverse_runs(line.items, -1);
    }
  }

  // Splits a line into words: a space glyph ends one, and so does a gap past
  // the word gap of the two glyphs either side of it, which is the spaced
  // one when both their fonts mark their words with space glyphs on the line
  // (so a font that places its words by position alone keeps its narrower
  // gap beside one that draws spaces, a placeholder of realistic text for
  // one), or a glyph drawn over the one read ahead of it. The line's glyphs
  // fall into segments drawn one straight after another; a font marks its
  // words with spaces in a segment when a space glyph, whatever font draws
  // it, is drawn there just after one of its glyphs (a producer that takes
  // every space from one face). Spaces drawn apart from a line's words (a
  // string of hyphens placed later into the gaps of a hymn's syllables)
  // leave the words as they were, and so does a placeholder's space, drawn
  // just after a glyph of the placeholder. On such a line a word a producer
  // sets apart as an operator of its own takes the narrower repositioned gap
  // (repositioned()), and glyphs of vertical writing take the spaced gap.
  void split_words(RawLine& line) const {
    std::vector<std::size_t> draws;
    draws.reserve(line.items.size());
    for (const std::size_t i : line.items) {
      draws.push_back(items_[i].draw);
    }
    std::ranges::sort(draws);
    // segment[k] is the segment of draws[k].
    std::vector<std::size_t> segment(draws.size(), 0);
    for (std::size_t k = 1; k < draws.size(); ++k) {
      segment[k] = segment[k - 1] + (draws[k] == draws[k - 1] + 1 ? 0 : 1);
    }
    const auto segment_of = [&](std::size_t draw) {
      return segment[static_cast<std::size_t>(std::ranges::lower_bound(draws, draw) -
                                              draws.begin())];
    };
    std::vector<std::pair<std::size_t, std::string_view>> spacing;
    for (std::size_t k = 1; k < draws.size(); ++k) {
      if (glyphs_[draws[k]].is_space() && draws[k] == draws[k - 1] + 1 &&
          !glyphs_[draws[k - 1]].is_space()) {
        const std::pair<std::size_t, std::string_view> mark(segment[k],
                                                            font_name(glyphs_[draws[k - 1]]));
        if (std::ranges::find(spacing, mark) == spacing.end()) {
          spacing.push_back(mark);
        }
      }
    }
    const auto draws_spaces = [&](const Item& item) {
      const std::pair<std::size_t, std::string_view> mark(segment_of(item.draw),
                                                          font_name(glyphs_[item.draw]));
      return std::ranges::find(spacing, mark) != spacing.end();
    };
    // The word under way starts at start when open; previous is its last
    // glyph.
    bool open = false;
    std::size_t start = 0;
    std::size_t previous = 0;
    for (std::size_t k = 0; k < line.items.size(); ++k) {
      const Item& item = items_[line.items[k]];
      if (glyphs_[item.draw].is_space()) {
        if (open) {
          line.words.emplace_back(start, k);
          open = false;
        }
        continue;
      }
      if (open) {
        const Item& before = items_[line.items[previous]];
        const double gap = std::max(item.low - before.high, before.low - item.high);
        const bool unspaced = ideographic(first_code_point(glyphs_[item.draw].text)) &&
                              ideographic(first_code_point(glyphs_[before.draw].text));
        // On a right-to-left line the reading runs against the positions, so
        // a glyph drawn over the one read ahead of it is one whose advance
        // overlaps it.
        const bool over = line.right_to_left ? drawn_over(before, item) && drawn_over(item, before)
                                             : drawn_over(before, item);
        // Glyphs of vertical writing stand in cells a full em tall, so a gap
        // between two of them is letter spacing up to the spaced threshold
        // (pdfium-0049: TJ numbers leave 0.2 em inside a vertical "Hello");
        // the narrower one was measured on proportional horizontal letters,
        // about half an em wide.
        const bool vertical = glyphs_[before.draw].vertical && glyphs_[item.draw].vertical;
        const bool spaced = vertical || (draws_spaces(before) && draws_spaces(item));
        const double gap_limit = !spaced                      ? TextLayout::word_gap
                                 : repositioned(before, item) ? repositioned_word_gap
                                                              : TextLayout::spaced_word_gap;
        const double limit = unspaced ? ideographic_gap : gap_limit;
        if (over || gap > limit * std::max(before.size, item.size)) {
          line.words.emplace_back(start, k);
          open = false;
        }
      }
      if (!open) {
        start = k;
        open = true;
      }
      previous = k;
    }
    if (open) {
      line.words.emplace_back(start, line.items.size());
    }
  }

  // Whether item opens a text-showing operator drawn straight after the one
  // that before closes, before advancing and each operator showing other
  // glyphs: a producer that draws its spaces sets a word anew after a word
  // break it draws no space for, as an operator of its own after a new text
  // position (dccc-0124: "Jahre" ends one TJ and "anführ-" opens the next 0.2
  // of the size further on, a justified column). The gap between the two is
  // read with repositioned_word_gap. The strings of one TJ array keep the
  // spaced gap, because a TJ number between them is letter spacing as often
  // as a word break ("Sp" "aced", "TRAV" "EL", "3." "14"); so does letter
  // spacing a producer sets glyph by glyph, one operator each, and a glyph
  // of no advance, which gives no place its operator's text ends.
  [[nodiscard]] bool repositioned(const Item& before, const Item& item) const {
    if (before.high <= before.low || item.draw != before.draw + 1 || before.draw == 0 ||
        item.draw + 1 >= glyphs_.size()) {
      return false;
    }
    const auto same_operator = [&](std::size_t a, std::size_t b) {
      return glyphs_[a].stream == glyphs_[b].stream && glyphs_[a].operation == glyphs_[b].operation;
    };
    return !same_operator(before.draw, item.draw) && same_operator(before.draw - 1, before.draw) &&
           same_operator(item.draw, item.draw + 1);
  }

  [[nodiscard]] std::vector<std::size_t> order_blocks(
      const std::vector<std::vector<std::size_t>>& groups,
      const std::map<std::int64_t, std::size_t>& rank) const {
    // The dominant direction: the bucket drawing the most glyphs.
    std::map<int, std::size_t> weight;
    std::size_t rtl_lines = 0;
    for (const RawLine& line : lines_) {
      weight[line.bucket] += line.items.size();
      rtl_lines += line.right_to_left ? 1U : 0U;
    }
    int dominant = 0;
    std::size_t best = 0;
    for (const auto& [bucket, count] : weight) {
      if (count > best) {
        best = count;
        dominant = bucket;
      }
    }
    const Frame frame(dominant);
    std::vector<Rect> boxes;
    boxes.reserve(groups.size());
    for (const std::vector<std::size_t>& group : groups) {
      Rect box;
      for (const std::size_t l : group) {
        for (const std::size_t i : lines_[l].items) {
          const GlyphRecord& glyph = glyphs_[items_[i].draw];
          const auto add = [&](double x, double y) {
            box.add(key(frame.along(x, y)), key(frame.across(x, y)));
          };
          if (glyph.box.is_empty()) {
            add(glyph.origin.x, glyph.origin.y);
          } else {
            add(glyph.box.left, glyph.box.bottom);
            add(glyph.box.right, glyph.box.bottom);
            add(glyph.box.left, glyph.box.top);
            add(glyph.box.right, glyph.box.top);
          }
        }
      }
      boxes.push_back(box);
    }
    std::vector<std::size_t> order = XyCut::order(boxes, 2 * rtl_lines > lines_.size());
    if (!rank.empty()) {
      std::vector<std::size_t> place(groups.size(), std::numeric_limits<std::size_t>::max());
      for (std::size_t g = 0; g < groups.size(); ++g) {
        for (const std::size_t l : groups[g]) {
          for (const std::size_t i : lines_[l].items) {
            const GlyphRecord& glyph = glyphs_[items_[i].draw];
            if (glyph.stream != 0 || glyph.mcid < 0) {
              continue;
            }
            if (const auto found = rank.find(glyph.mcid); found != rank.end()) {
              place[g] = std::min(place[g], found->second);
            }
          }
        }
      }
      std::ranges::stable_sort(order, {}, [&](std::size_t g) { return place[g]; });
    }
    return order;
  }

  TextLayout::Block emit_block(const std::vector<std::size_t>& group, std::size_t offset,
                               std::vector<std::size_t>& order) const {
    std::vector<std::size_t> lines = group;
    std::ranges::stable_sort(lines, [&](std::size_t a, std::size_t b) {
      if (key(lines_[a].across_high) != key(lines_[b].across_high)) {
        return key(lines_[a].across_high) > key(lines_[b].across_high);
      }
      return key(lines_[a].along_low) < key(lines_[b].along_low);
    });
    TextLayout::Block block;
    block.first = offset + order.size();
    for (const std::size_t l : lines) {
      const RawLine& raw = lines_[l];
      TextLayout::Line line;
      line.first = offset + order.size();
      line.angle = static_cast<double>(raw.bucket);
      line.direction = raw.right_to_left ? "rtl" : (raw.bucket == -90 ? "ttb" : "ltr");
      const std::size_t base = order.size();
      for (const std::size_t i : raw.items) {
        const GlyphRecord& glyph = glyphs_[items_[i].draw];
        order.push_back(glyph.draw);
        if (glyph.box.is_empty()) {
          line.box.add(glyph.origin.x, glyph.origin.y);
        } else {
          line.box.add(glyph.box);
        }
      }
      for (const auto& [first, last] : raw.words) {
        TextLayout::Word word;
        word.first = offset + base + first;
        word.last = offset + base + last;
        for (std::size_t k = first; k < last; ++k) {
          const GlyphRecord& glyph = glyphs_[items_[raw.items[k]].draw];
          word.text += glyph.text;
          word.hidden = static_cast<std::uint16_t>(word.hidden | glyph.hidden);
          if (glyph.box.is_empty()) {
            word.box.add(glyph.origin.x, glyph.origin.y);
          } else {
            word.box.add(glyph.box);
          }
        }
        line.words.push_back(std::move(word));
      }
      line.last = offset + order.size();
      block.box.add(line.box);
      block.lines.push_back(std::move(line));
    }
    block.last = offset + order.size();
    return block;
  }

  const std::vector<GlyphRecord>& glyphs_;
  std::vector<Item> items_;
  std::vector<RawLine> lines_;
  std::vector<Docstrum::Rule> rules_;
};

}  // namespace

TextLayout::Result TextLayout::run(const std::vector<GlyphRecord>& glyphs,
                                   const std::vector<std::size_t>& members, std::size_t offset,
                                   const std::map<std::int64_t, std::size_t>& rank,
                                   const std::vector<Rect>& rules) {
  return Builder(glyphs, members, rules).run(offset, rank);
}

void TextLayout::join_hyphen(Line& before, const Line& after) {
  if (before.words.empty() || after.words.empty()) {
    return;
  }
  Word& last = before.words.back();
  const char32_t hyphen = last_code_point(last.text);
  if (hyphen != U'-' && hyphen != 0xAD) {
    return;
  }
  // A word of letters before the hyphen, so a dash at a line's end is not a
  // hyphenation.
  if (!letter(first_code_point(last.text))) {
    return;
  }
  last.hyphenated = lower_case(first_code_point(after.words.front().text));
}

}  // namespace pdfkit::text
