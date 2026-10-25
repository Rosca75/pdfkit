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
#include <limits>
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

MatchFinder::MatchFinder(const text::PageText& page) : page_(&page) {
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
  if (position_.empty() && !glyphs.empty()) {
    position_.assign(glyphs.size(), none);
    for (std::size_t p = 0; p < page_->order.size(); ++p) {
      if (page_->order[p] < position_.size()) {
        position_[page_->order[p]] = p;
      }
    }
  }
  std::size_t start = 0;
  while (start < glyphs.size()) {
    // One string operand: consecutive records of one walk, operator and
    // operand.
    std::size_t end = start + 1;
    while (end < glyphs.size() && glyphs[end].stream == glyphs[start].stream &&
           glyphs[end].operation == glyphs[start].operation &&
           glyphs[end].operand == glyphs[start].operand) {
      ++end;
    }
    std::string bytes;
    std::vector<std::size_t> owner;
    for (std::size_t g = start; g < end; ++g) {
      for (std::size_t i = glyphs[g].code_length; i > 0; --i) {
        bytes.push_back(static_cast<char>((glyphs[g].code >> (8U * (i - 1))) & 0xFFU));
        owner.push_back(g);
      }
    }
    if (match.folding.text.case_fold) {
      bytes = lower_ascii(std::move(bytes));
    }
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
    start = end;
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
