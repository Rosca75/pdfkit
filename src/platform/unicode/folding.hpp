// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Folding is the text folding a search applies on both sides, docs/DESIGN.md
// section 11, each step a flag of the caller's options:
//   - case: Unicode simple case folding (CaseFolding.txt status C and S, the
//     Unicode Standard section 3.13), so "ß" stays "ß" and "İ" stays "İ"
//     while the Greek final sigma folds to sigma;
//   - diacritics: canonical decomposition (UnicodeData.txt field 5, applied
//     until nothing decomposes further) with the combining marks
//     (General_Category Mn and Me) removed, so "É" reads "E" while "Ł",
//     which has no decomposition, stays;
//   - whitespace: every run of white space (General_Category Zs, Zl, Zp and
//     the controls of the White_Space property) reads as one space.
// Under every option a positional presentation form (a decomposition of
// UnicodeData.txt tagged <initial>, <medial>, <final> or <isolated>: the
// Arabic presentation forms and their ligatures) reads as its letters,
// because it is the letter drawn in the shape its place in a word takes, and
// a reader that undoes the shaping (PDFium does) writes the letters. The
// other compatibility forms (a full-width letter, U+00DF, a superscript
// digit) stay what they are.
// A folded text keeps, for each of its code points, the index of the source
// code point it came from, so a match found in folded text maps back to the
// source. The tables are generated from Unicode 18.0.0 by
// build/unicode/generate.py into tables.inc.

#ifndef PDFKIT_PLATFORM_UNICODE_FOLDING_HPP
#define PDFKIT_PLATFORM_UNICODE_FOLDING_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::platform::unicode {

class Folding {
 public:
  struct Options {
    bool case_fold = true;
    bool diacritics = true;
    bool whitespace = true;
  };

  // A folded text and, per code point of it, the index of the source code
  // point it came from.
  struct Mapped {
    std::u32string text;
    std::vector<std::size_t> source;
  };

  // The code points of UTF-8 text; an ill-formed sequence reads as U+FFFD.
  [[nodiscard]] static std::u32string decode(std::string_view utf8);
  // The UTF-8 form of text; a surrogate or a value past U+10FFFF writes
  // U+FFFD.
  [[nodiscard]] static std::string encode(std::u32string_view text);

  [[nodiscard]] static bool is_white_space(char32_t code_point) noexcept;
  [[nodiscard]] static bool is_mark(char32_t code_point) noexcept;
  // The simple case folding of one code point; itself when it has none.
  [[nodiscard]] static char32_t simple_case_fold(char32_t code_point) noexcept;
  // The full canonical decomposition of one code point; itself when it has
  // none.
  [[nodiscard]] static std::u32string decompose(char32_t code_point);
  // The letters of a positional presentation form; itself when it is none.
  [[nodiscard]] static std::u32string unshape(char32_t code_point);

  // The case and diacritic folding of one code point, appended to out (zero
  // code points for a removed mark, several for a decomposition).
  static void fold_code_point(char32_t code_point, const Options& options, std::u32string& out);

  // text folded under options, white space collapsed when options say so.
  [[nodiscard]] static Mapped fold(std::u32string_view text, const Options& options);
  // UTF-8 text folded, as UTF-8.
  [[nodiscard]] static std::string fold_utf8(std::string_view text, const Options& options);
};

}  // namespace pdfkit::platform::unicode

#endif  // PDFKIT_PLATFORM_UNICODE_FOLDING_HPP
