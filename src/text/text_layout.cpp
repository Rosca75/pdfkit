// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The layout pass: glyphs to raw lines by direction and baseline, raw lines to
// ordered glyphs and words, lines to Docstrum blocks, blocks to reading order
// by XY-cut in the page's dominant direction. Every geometric key goes
// through key() before it is compared.

#include "text/text_layout.hpp"

#include "platform/utf8_sequence.hpp"
#include "text/docstrum.hpp"
#include "text/xy_cut.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

namespace pdfkit::text {

namespace {

// A gap ends a word from this many font sizes on a line that draws no space
// glyph (words set apart by position alone), and from spaced_word_gap on a
// line that marks its words with space glyphs, whose other gaps are letter
// spacing and kerning.
constexpr double word_gap = 0.1;
constexpr double spaced_word_gap = 0.3;
// Between two ideographic, kana, hangul or full-width glyphs, which no space
// separates in the scripts that use them, a gap ends a word only from this
// many font sizes.
constexpr double ideographic_gap = 1.0;
constexpr double line_gap = 2.0;
constexpr double baseline_tolerance = 0.2;
// A glyph drawn this many font sizes before the one drawn just ahead of it on
// its baseline starts a new run of the content.
constexpr double run_backstep = 0.5;
// Runs of at most this many glyphs take their place by position.
constexpr std::size_t short_run = 2;
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
    if (glyph.box.is_empty()) {
      const double across = frame.across(glyph.origin.x, glyph.origin.y);
      item.across_low = item.across_high = key(across);
    } else {
      const Rect& box = glyph.box;
      const double corners[4] = {frame.across(box.left, box.bottom),
                                 frame.across(box.right, box.bottom),
                                 frame.across(box.left, box.top), frame.across(box.right, box.top)};
      item.across_low = key(*std::min_element(std::begin(corners), std::end(corners)));
      item.across_high = key(*std::max_element(std::begin(corners), std::end(corners)));
    }
    return item;
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
      for (std::vector<std::size_t>& cluster : clusters) {
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

  // The glyphs of one baseline in visual order along it. The content's runs
  // (glyphs drawn one after another, each at or after the one before) are kept
  // whole and ordered by where they start, so a string drawn over another
  // (a name overprinted with X, a shadow offset) reads as two strings rather
  // than as their letters interleaved; a run of one or two glyphs (an accent,
  // a glyph drawn out of turn, right-to-left text drawn in logical order)
  // takes its place by position.
  [[nodiscard]] std::vector<std::size_t> visual_sequence(std::vector<std::size_t> cluster) const {
    std::ranges::stable_sort(cluster, {}, [&](std::size_t i) { return items_[i].draw; });
    std::vector<std::vector<std::size_t>> runs;
    for (const std::size_t i : cluster) {
      const Item& item = items_[i];
      if (runs.empty()) {
        runs.emplace_back();
      } else {
        const Item& before = items_[runs.back().back()];
        if (item.low < before.low - (run_backstep * std::max(before.size, item.size))) {
          runs.emplace_back();
        }
      }
      runs.back().push_back(i);
    }
    const auto by_position = [&](std::size_t a, std::size_t b) {
      return std::pair(items_[a].low, items_[a].draw) < std::pair(items_[b].low, items_[b].draw);
    };
    for (std::vector<std::size_t>& run : runs) {
      std::ranges::stable_sort(run, by_position);
    }
    std::ranges::stable_sort(
        runs, [&](const std::vector<std::size_t>& a, const std::vector<std::size_t>& b) {
          return by_position(a.front(), b.front());
        });
    std::vector<std::size_t> sequence;
    sequence.reserve(cluster.size());
    for (const std::vector<std::size_t>& run : runs) {
      if (run.size() > short_run || sequence.empty()) {
        sequence.insert(sequence.end(), run.begin(), run.end());
        continue;
      }
      for (const std::size_t i : run) {
        const auto at = std::ranges::upper_bound(sequence, i, by_position);
        sequence.insert(at, i);
      }
    }
    return sequence;
  }

  // The reading direction of each glyph's text: right to left, left to
  // right (letters and digits), or neutral.
  [[nodiscard]] int strength(std::size_t item) const {
    const std::string& text = glyphs_[items_[item].draw].text;
    std::size_t at = 0;
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

  void split_words(RawLine& line) const {
    const bool spaced = std::ranges::any_of(
        line.items, [&](std::size_t i) { return glyphs_[items_[i].draw].is_space(); });
    const double gap_limit = spaced ? spaced_word_gap : word_gap;
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
        if (gap > (unspaced ? ideographic_gap : gap_limit) * std::max(before.size, item.size)) {
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
