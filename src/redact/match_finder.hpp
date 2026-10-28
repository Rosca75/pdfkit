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
// locate() is the one search pdfkit_find reports and pdfkit_redact_text
// removes (occurrences() is the same run flattened into the stretches a
// redaction takes), so the count shown to a caller is the count removed:
// every occurrence of a search in the reading order, in the other orders and
// in the codes, each with the stretches of the glyph list it covers and the
// order that found it; rectangles(), hidden() and context() describe one.

#ifndef PDFKIT_REDACT_MATCH_FINDER_HPP
#define PDFKIT_REDACT_MATCH_FINDER_HPP

#include "platform/unicode/folding.hpp"
#include "text/page_text.hpp"
#include "text/rect.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
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

  // The order a reader reads the page in: pdfkit's reading order, the
  // glyphs as drawn (and the codes of one string operand, which are drawn in
  // their order), and the rows read along the baseline forwards and
  // backwards.
  enum class Order : std::uint8_t { reading, drawing, rows_ltr, rows_rtl };

  // One occurrence of a search: its stretches [first, last) of the page's
  // reading-order glyph list in ascending order (one when the reading order
  // found it, several when another order or the codes found it), the first
  // order that found it and its text as that order reads it. lead is false
  // when the run's first stretch was left out (the reading order had found
  // its glyphs), so no stretch of it draws the placeholder.
  struct Located {
    std::uint32_t page = 0;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    Order order = Order::reading;
    std::string text;
    bool lead = true;
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
  // The same, given what find() returned for the match.
  [[nodiscard]] std::vector<Occurrence> find_orders(const Match& match, std::size_t index,
                                                    std::span<const Occurrence> known);

  // Every occurrence of a search match on the page, in the order the
  // redaction search finds them: the reading order's, then each other
  // order's that the reading order does not cover, then the code runs whose
  // stretches no earlier occurrence overlaps. An occurrence found in another
  // order or in the codes keeps the stretches it covers together.
  [[nodiscard]] std::vector<Located> locate(const Match& match);

  // locate() flattened: one occurrence per stretch, the first stretch of
  // each located occurrence carrying the placeholder; what
  // pdfkit_redact_text removes for a search.
  [[nodiscard]] std::vector<Occurrence> occurrences(const Match& match, std::size_t index);

  // The name pdfkit_find writes for an order.
  [[nodiscard]] static std::string_view order_name(Order order) noexcept;

  // The device boxes of an occurrence's glyphs: one per reading line it
  // touches, the union of its glyphs' boxes there (a glyph without a box
  // spans its origin to the end of its advance), in reading order.
  [[nodiscard]] std::vector<text::Rect> rectangles(const Located& located);

  // The hidden classes (a mask of text::HiddenClass bits) of any of its
  // glyphs.
  [[nodiscard]] std::uint16_t hidden(const Located& located) const;

  // Up to words words of the page's reading-order text on each side of the
  // occurrence (a word of the page as pdfkit_text reads it, the hidden
  // blocks after the visible ones, a duplicate copy read once), joined by a
  // space; the words wholly before its first glyph and wholly after its
  // last, so a value inside a longer word leaves that word out of both.
  [[nodiscard]] std::pair<std::string, std::string> context(const Located& located,
                                                            std::size_t words);

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

  // One glyph of another order: its draw index, and whether a word gap
  // precedes it.
  struct Step {
    std::size_t draw = 0;
    bool gap = false;
  };

  // One of the other orders folded: without its white space under the
  // whitespace folding, gap naming the characters a word gap precedes, step
  // the step each character came from.
  struct OrderHaystack {
    std::u32string text;
    std::vector<std::size_t> step;
    std::vector<bool> gap;
  };

  [[nodiscard]] const Haystack& haystack(const Folding& folding);
  // The runs another order finds that the known occurrences do not cover,
  // and the runs the codes spell, each split into its stretches.
  [[nodiscard]] std::vector<Located> order_runs(const Match& match,
                                                std::span<const Occurrence> known);
  [[nodiscard]] std::vector<Located> code_runs(const Match& match);
  // The glyphs of a run in the order that read it, joined into its text.
  [[nodiscard]] std::string run_text(const std::vector<std::size_t>& draws,
                                     const std::vector<bool>& gaps) const;
  static void flatten(const Located& located, std::size_t index, const std::string& placeholder,
                      std::vector<Occurrence>& out);
  void build(bool hyphenation, std::vector<Unit>& units) const;
  // The reading-order position of each draw index.
  void index_positions();
  void build_orders();
  // Whether the other orders read the glyph of draw index draw (a duplicate
  // copy is read with its partner), and the glyph appended to steps when it
  // shows text, carrying the word gap gap says precedes it.
  [[nodiscard]] bool in_orders(std::size_t draw) const;
  void add_step(std::vector<Step>& steps, std::size_t draw, bool& gap) const;
  [[nodiscard]] const std::vector<OrderHaystack>& order_haystacks(const Folding& folding);

  const text::PageText* page_;
  std::set<std::string> left_out_;
  std::vector<Unit> plain_;
  std::vector<Unit> joined_;
  std::map<unsigned, Haystack> folded_;
  // The other orders (the drawing order, the rows read forwards and
  // backwards), built on first use of find_orders, and their folded forms,
  // each glyph's text folded once for all three.
  std::vector<std::vector<Step>> orders_;
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
  // The reading line of each position and the page's words in reading
  // order, built on first use of rectangles and context.
  struct WordAt {
    std::size_t first = 0;
    std::size_t last = 0;
    std::string text;
  };
  std::vector<const text::TextLayout::Line*> line_of_;
  std::vector<WordAt> words_;
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_MATCH_FINDER_HPP
