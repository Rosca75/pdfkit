// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// MatchFinder finds a caller's matches on one page, docs/DESIGN.md section
// 11. A match is a glyph range from an earlier pdfkit_text (positions in the
// page's reading-order glyph list) or a search string with folding options.
// A search runs over the page's text exactly as pdfkit_text writes it (words
// joined by a space, lines by a line feed, blocks by an empty line, the
// hidden blocks after the visible ones, a duplicate-only word read once),
// each character naming the glyph it came from, so a match in the text is a
// range of glyph positions. Both sides are folded alike (Folding); an
// inferred word gap is a space; under the hyphenation folding the hyphen of
// a word joined across a line break and the break itself read as no
// character, so "Quarter-" / "field" matches "Quarterfield" and the hyphen
// goes with the match. A match is one range from its first glyph to its
// last, so it crosses operators, fonts, TJ numbers, lines and the hyphen.
// merge() applies the overlap rule: overlapping or adjacent occurrences with
// one placeholder become one, and where placeholders differ the occurrence
// that starts earlier keeps the overlap.

#ifndef PDFKIT_REDACT_MATCH_FINDER_HPP
#define PDFKIT_REDACT_MATCH_FINDER_HPP

#include "platform/unicode/folding.hpp"
#include "text/page_text.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::redact {

class MatchFinder {
 public:
  // The caller's folding: the three text foldings and the hyphenation join.
  struct Folding {
    platform::unicode::Folding::Options text;
    bool hyphenation = true;
  };

  struct Match {
    enum class Kind : std::uint8_t { search, glyphs };
    Kind kind = Kind::search;
    // A search: the string, as UTF-8, and its folding.
    std::string search;
    Folding folding;
    // A glyph range: the page (1-based) and [first, last) positions in its
    // reading-order glyph list.
    std::uint32_t page = 0;
    std::size_t first = 0;
    std::size_t last = 0;
    // The document fingerprint the extraction reported with the range.
    std::string fingerprint;
    std::string placeholder;
  };

  struct Occurrence {
    // The match it belongs to (its index in the plan).
    std::size_t match = 0;
    std::uint32_t page = 0;
    // [first, last) positions in the page's reading-order glyph list.
    std::size_t first = 0;
    std::size_t last = 0;
    std::string placeholder;
  };

  // page must outlive the finder.
  explicit MatchFinder(const text::PageText& page);

  // The occurrences of a search match on the page, in reading order; a
  // string that folds to nothing finds nothing.
  [[nodiscard]] std::vector<Occurrence> find(const Match& match, std::size_t index);

  // The occurrences of a search match in the codes the page's strings show:
  // per string operand, the bytes of its codes, searched for the match's
  // byte forms (byte_forms), each found run of glyphs an occurrence for every
  // stretch of it that is contiguous in reading order. It removes what a font
  // that reads its codes otherwise still spells in the file's bytes (a code
  // the decoding ladder could not read, a font whose glyphs are spread out of
  // reading order), which the self-check's scan of string operands and an
  // extractor reading the codes as Latin-1 would find.
  [[nodiscard]] std::vector<Occurrence> find_codes(const Match& match, std::size_t index);

  // The byte forms of a search string the raw scans look for: its UTF-8, its
  // Latin-1 when every character fits, and its UTF-16BE, its white space
  // collapsed when white space folds and its ASCII letters in lower case when
  // case folds.
  [[nodiscard]] static std::vector<std::string> byte_forms(const Match& match);

  // bytes with its ASCII letters in lower case.
  [[nodiscard]] static std::string lower_ascii(std::string bytes);

  // The page's text over [first, last), as pdfkit_text writes it: the string
  // a glyph-range match redacts.
  [[nodiscard]] std::string text(std::size_t first, std::size_t last) const;

  // The ranges [first, last) of code points of text where a search match
  // occurs under its text folding, in order and not overlapping: the search
  // over a string that is not a page's text (a property value, a string
  // object, a metadata value).
  [[nodiscard]] static std::vector<std::pair<std::size_t, std::size_t>> ranges_in(
      std::u32string_view text, const Match& match);

  // The occurrences of one page with the overlap rule applied, sorted by
  // first position.
  [[nodiscard]] static std::vector<Occurrence> merge(std::vector<Occurrence> occurrences);

 private:
  // One character of the page's text and the glyph position it came from
  // (none for a separator).
  struct Unit {
    char32_t code_point = 0;
    std::size_t position = 0;
    bool separator = false;
  };

  struct Haystack {
    std::u32string text;
    std::vector<std::size_t> unit;  // per folded code point, its unit
  };

  [[nodiscard]] const Haystack& haystack(const Folding& folding);
  void build(bool hyphenation, std::vector<Unit>& units) const;

  const text::PageText* page_;
  std::vector<Unit> plain_;
  std::vector<Unit> joined_;
  std::map<unsigned, Haystack> folded_;
  // The reading-order position of each draw index, built on first use.
  std::vector<std::size_t> position_;
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_MATCH_FINDER_HPP
