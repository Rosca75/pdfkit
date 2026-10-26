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
// find_orders() searches the orders other readers read the page in (the
// glyphs as drawn, and each row of glyphs on one baseline read along it in
// either direction), so a phrase pdfkit's reading order splits is still
// found wherever another reader puts its words side by side.
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
#include <set>
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

  // page must outlive the finder. The glyphs of the fonts named in
  // left_out (the placeholder faces, when a written output is searched) are
  // left out of the other orders of find_orders, because a placeholder
  // squeezed into or overflowing its hole lies among the kept glyphs along
  // the baseline, and its letters are no text of the document.
  explicit MatchFinder(const text::PageText& page, std::set<std::string> left_out = {});

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

  // The occurrences of a search match that another reader's order exposes
  // and find() does not, docs/DESIGN.md section 11: the page's glyphs read
  // in the order they were drawn, and the page's rows (the lines of every
  // block whose baselines agree, so across a column gap) read along the
  // baseline from either end. In these orders a word gap is read from the
  // positions (a space glyph, or a glyph that does not start where the one
  // before it ended, on its baseline or within half the size of it), and
  // under the whitespace folding a space of the string matches a word gap or
  // none, because readers disagree on the gaps they infer; inside a word of
  // the string no gap is allowed. Each found run is split into the stretches
  // that are contiguous in reading order, the first carrying the
  // placeholder, and the glyphs find() or an earlier run already covers are
  // left out. The hyphenation join is the reading order's alone.
  [[nodiscard]] std::vector<Occurrence> find_orders(const Match& match, std::size_t index);

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

  // One of the other orders folded: without its white space under the
  // whitespace folding, gap naming the characters a word gap precedes.
  struct OrderHaystack {
    std::u32string text;
    std::vector<std::size_t> unit;
    std::vector<bool> gap;
  };

  [[nodiscard]] const Haystack& haystack(const Folding& folding);
  void build(bool hyphenation, std::vector<Unit>& units) const;
  // The reading-order position of each draw index.
  void index_positions();
  void build_orders();
  // Whether the other orders read the glyph of draw index draw (a duplicate
  // copy is read with its partner), and its code points appended to units,
  // a separator first when gap says a word gap precedes it.
  [[nodiscard]] bool in_orders(std::size_t draw) const;
  void add_units(std::vector<Unit>& units, std::size_t draw, bool& gap) const;
  [[nodiscard]] const std::vector<OrderHaystack>& order_haystacks(const Folding& folding);

  const text::PageText* page_;
  std::set<std::string> left_out_;
  std::vector<Unit> plain_;
  std::vector<Unit> joined_;
  std::map<unsigned, Haystack> folded_;
  // The other orders' units (the drawing order, the rows read forwards and
  // backwards), built on first use of find_orders, and their folded forms.
  std::vector<std::vector<Unit>> orders_;
  std::map<unsigned, std::vector<OrderHaystack>> order_folded_;
  // The reading-order position of each draw index, built on first use.
  std::vector<std::size_t> position_;
  // The code bytes of each string operand (consecutive records of one walk,
  // operator and operand), as drawn and with ASCII lowered, and the draw
  // index each byte came from; built on first use of find_codes.
  struct Operand {
    std::string bytes;
    std::string lowered;
    std::vector<std::size_t> owner;
  };
  std::vector<Operand> operands_;
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_MATCH_FINDER_HPP
