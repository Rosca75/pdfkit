// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The folding steps over the generated tables. Every table is sorted, so a
// lookup is a binary search. A positional presentation form is first
// replaced by its letters, each then folded on its own. A code point is
// folded as decomposition, mark removal, case folding, then decomposition and
// mark removal again, because a case folding can produce a decomposable
// letter (U+212B ANGSTROM SIGN folds to U+00E5, which decomposes to "a" and a
// ring above).

#include "platform/unicode/folding.hpp"

#include "platform/utf8_sequence.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace pdfkit::platform::unicode {

namespace {

struct CaseFold {
  char32_t from;
  char32_t to;
};

struct Decomposition {
  char32_t code;
  std::uint16_t offset;
  std::uint8_t length;
};

struct CodeRange {
  char32_t first;
  char32_t last;
};

#include "platform/unicode/tables.inc"

template <std::size_t N>
[[nodiscard]] bool in_ranges(const std::array<CodeRange, N>& table, char32_t code_point) noexcept {
  const auto found = std::ranges::upper_bound(table, code_point, {}, &CodeRange::first);
  return found != table.begin() && code_point <= std::prev(found)->last;
}

void strip_marks(std::u32string_view decomposed, std::u32string& out) {
  for (const char32_t value : decomposed) {
    if (!Folding::is_mark(value)) {
      out.push_back(value);
    }
  }
}

}  // namespace

std::u32string Folding::decode(std::string_view utf8) {
  std::u32string out;
  out.reserve(utf8.size());
  std::size_t at = 0;
  while (at < utf8.size()) {
    const Utf8Sequence sequence = next_utf8_sequence(utf8, at);
    out.push_back(sequence.valid ? sequence.code_point : U'�');
    at += std::max<std::size_t>(sequence.length, 1);
  }
  return out;
}

std::string Folding::encode(std::u32string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char32_t value : text) {
    const bool scalar = value <= 0x10FFFF && (value < 0xD800 || value > 0xDFFF);
    append_utf8(out, scalar ? value : U'�');
  }
  return out;
}

bool Folding::is_white_space(char32_t code_point) noexcept {
  return in_ranges(spaces, code_point);
}

bool Folding::is_mark(char32_t code_point) noexcept {
  return in_ranges(marks, code_point);
}

char32_t Folding::simple_case_fold(char32_t code_point) noexcept {
  const auto found = std::ranges::lower_bound(case_folds, code_point, {}, &CaseFold::from);
  return found != case_folds.end() && found->from == code_point ? found->to : code_point;
}

std::u32string Folding::decompose(char32_t code_point) {
  const auto found = std::ranges::lower_bound(decompositions, code_point, {}, &Decomposition::code);
  if (found == decompositions.end() || found->code != code_point) {
    return {code_point};
  }
  return {decomposition_data.begin() + found->offset,
          decomposition_data.begin() + found->offset + found->length};
}

std::u32string Folding::unshape(char32_t code_point) {
  // Every positional form lies in the presentation forms blocks, from the
  // first entry of the table to its last.
  if (code_point < positional_forms.front().code || code_point > positional_forms.back().code) {
    return {code_point};
  }
  const auto found =
      std::ranges::lower_bound(positional_forms, code_point, {}, &Decomposition::code);
  if (found == positional_forms.end() || found->code != code_point) {
    return {code_point};
  }
  return {positional_data.begin() + found->offset,
          positional_data.begin() + found->offset + found->length};
}

void Folding::fold_code_point(char32_t code_point, const Options& options, std::u32string& out) {
  if (const std::u32string letters = unshape(code_point);
      letters.size() != 1 || letters.front() != code_point) {
    for (const char32_t letter : letters) {
      fold_code_point(letter, options, out);
    }
    return;
  }
  if (!options.diacritics) {
    out.push_back(options.case_fold ? simple_case_fold(code_point) : code_point);
    return;
  }
  std::u32string bare;
  strip_marks(decompose(code_point), bare);
  if (!options.case_fold) {
    out += bare;
    return;
  }
  for (const char32_t value : bare) {
    strip_marks(decompose(simple_case_fold(value)), out);
  }
}

Folding::Mapped Folding::fold(std::u32string_view text, const Options& options) {
  Mapped mapped;
  mapped.text.reserve(text.size());
  mapped.source.reserve(text.size());
  std::u32string piece;
  bool in_space = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char32_t value = text[i];
    if (options.whitespace && is_white_space(value)) {
      if (!in_space) {
        mapped.text.push_back(U' ');
        mapped.source.push_back(i);
      }
      in_space = true;
      continue;
    }
    in_space = false;
    piece.clear();
    fold_code_point(value, options, piece);
    for (const char32_t folded : piece) {
      mapped.text.push_back(folded);
      mapped.source.push_back(i);
    }
  }
  return mapped;
}

std::string Folding::fold_utf8(std::string_view text, const Options& options) {
  return encode(fold(decode(text), options).text);
}

}  // namespace pdfkit::platform::unicode
