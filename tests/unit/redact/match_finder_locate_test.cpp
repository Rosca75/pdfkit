// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// MatchFinder::locate, the search pdfkit_find reports and pdfkit_redact_text
// removes (docs/DESIGN.md section 11): a value inside a longer word is an
// occurrence; one found only in the drawing order keeps its stretches
// together and names the order; a hidden occurrence carries its classes; the
// context runs to the page's edges and no further; and occurrences(), what a
// redaction removes, is locate() flattened, stretch for stretch.

#include "redact/match_finder.hpp"
#include "text/hidden_class.hpp"
#include "text/support/text_document.hpp"

#include <doctest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using pdfkit::redact::MatchFinder;
using pdfkit::test::TextDocument;
using pdfkit::text::PageText;

MatchFinder::Match search(std::string_view text) {
  MatchFinder::Match match;
  match.search = std::string(text);
  match.placeholder = "[REDACTED]";
  return match;
}

PageText page_of(TextDocument& document, const std::string& content) {
  document.page(content);
  return document.read();
}

}  // namespace

TEST_CASE("a value inside a longer word is an occurrence") {
  TextDocument document;
  const PageText page = page_of(document, "BT /F1 10 Tf 72 700 Td (Dear Smithson, regards) Tj ET");
  MatchFinder finder(page);
  const std::vector<MatchFinder::Located> found = finder.locate(search("smith"));
  REQUIRE(found.size() == 1);
  CHECK(found[0].order == MatchFinder::Order::reading);
  REQUIRE(found[0].ranges.size() == 1);
  CHECK(found[0].ranges[0].first == 5);
  CHECK(found[0].ranges[0].second == 10);
  CHECK(found[0].text == "Smith");
  const auto [before, after] = finder.context(found[0], 5);
  CHECK(before == "Dear");
  CHECK(after == "regards");
}

TEST_CASE("a value found only in the drawing order keeps its stretches together") {
  TextDocument document;
  // Two blocks whose lines interleave: the reading order splits "Bravo
  // Charlie", the drawing order reads it whole.
  const PageText page = page_of(
      document,
      "BT /F1 14 Tf 72 700 Td (Alpha Bravo) Tj 0 -24 Td (Charlie Delta) Tj 0 -24 Td (Echo) Tj "
      "ET BT /F1 14 Tf 74 688 Td (Golf Hotel) Tj 0 -24 Td (India Juliet) Tj 0 -24 Td (Kilo) "
      "Tj ET");
  MatchFinder finder(page);
  const std::vector<MatchFinder::Located> found = finder.locate(search("Bravo Charlie"));
  REQUIRE(found.size() == 1);
  CHECK(found[0].order == MatchFinder::Order::drawing);
  CHECK(found[0].ranges.size() == 2);
  CHECK(found[0].text == "Bravo Charlie");
  CHECK(MatchFinder::order_name(found[0].order) == "drawing");
  // One rectangle per reading line the glyphs lie on.
  CHECK(finder.rectangles(found[0]).size() == 2);
}

TEST_CASE("a hidden occurrence carries its classes") {
  TextDocument document;
  const PageText page =
      page_of(document, "BT /F1 10 Tf 72 700 Td (Shown) Tj 3 Tr 0 -14 Td (Ghostly) Tj ET");
  MatchFinder finder(page);
  const std::vector<MatchFinder::Located> found = finder.locate(search("ghost"));
  REQUIRE(found.size() == 1);
  CHECK((finder.hidden(found[0]) &
         pdfkit::text::hidden_bit(pdfkit::text::HiddenClass::render_invisible)) != 0);
  const std::vector<MatchFinder::Located> shown = finder.locate(search("Shown"));
  REQUIRE(shown.size() == 1);
  CHECK(finder.hidden(shown[0]) == 0);
}

TEST_CASE("the context stops at the page's edges") {
  TextDocument document;
  const PageText page =
      page_of(document, "BT /F1 10 Tf 72 700 Td (alpha beta gamma) Tj 0 -14 Td (delta) Tj ET");
  MatchFinder finder(page);
  using Pair = std::pair<std::string, std::string>;
  const std::vector<MatchFinder::Located> first = finder.locate(search("alpha"));
  REQUIRE(first.size() == 1);
  CHECK(finder.context(first[0], 5) == Pair{"", "beta gamma delta"});
  CHECK(finder.context(first[0], 1) == Pair{"", "beta"});
  CHECK(finder.context(first[0], 0) == Pair{"", ""});
  const std::vector<MatchFinder::Located> last = finder.locate(search("delta"));
  REQUIRE(last.size() == 1);
  CHECK(finder.context(last[0], 5) == Pair{"alpha beta gamma", ""});
}

TEST_CASE("what a redaction removes is what locate finds, stretch for stretch") {
  const std::vector<std::string> contents{
      "BT /F1 10 Tf 72 700 Td (Zebulon and zebulon, Zebulonian) Tj ET",
      "BT /F1 14 Tf 72 700 Td (Alpha Bravo) Tj 0 -24 Td (Charlie Delta) Tj 0 -24 Td (Echo) Tj "
      "ET BT /F1 14 Tf 74 688 Td (Golf Hotel) Tj 0 -24 Td (India Juliet) Tj 0 -24 Td (Kilo) "
      "Tj ET",
      "BT /F1 10 Tf 72 700 Td (Quarterly Annual) Tj 0 -12 Td (more text here) Tj ET BT /F1 10 "
      "Tf 320 700 Td (Report follows) Tj 0 -12 Td (second column) Tj ET",
      "BT /F1 10 Tf 72 700 Td (T) Tj 4.44 -2.2 Td (E) Tj 4.9 2.2 Td (X) Tj 12 0 Td (logo) Tj ET",
  };
  const std::vector<std::string> values{"zebulon", "Bravo Charlie", "Annual Report", "TeX", "o",
                                        "e"};
  for (const std::string& content : contents) {
    TextDocument document;
    const PageText page = page_of(document, content);
    for (const std::string& value : values) {
      INFO(content, " / ", value);
      MatchFinder finder(page);
      const MatchFinder::Match match = search(value);
      // The redaction search as it reads: the reading order, the other
      // orders, the codes no earlier occurrence overlaps.
      std::vector<MatchFinder::Occurrence> expected = finder.find(match, 3);
      for (MatchFinder::Occurrence& other : finder.find_orders(match, 3, expected)) {
        expected.push_back(std::move(other));
      }
      for (const MatchFinder::Occurrence& coded : finder.find_codes(match, 3)) {
        bool read = false;
        for (const MatchFinder::Occurrence& other : expected) {
          read = read || (other.first < coded.last && coded.first < other.last);
        }
        if (!read) {
          expected.push_back(coded);
        }
      }
      MatchFinder again(page);
      const std::vector<MatchFinder::Occurrence> got = again.occurrences(match, 3);
      REQUIRE(got.size() == expected.size());
      for (std::size_t i = 0; i < got.size(); ++i) {
        CHECK(got[i].match == 3);
        CHECK(got[i].page == expected[i].page);
        CHECK(got[i].first == expected[i].first);
        CHECK(got[i].last == expected[i].last);
        CHECK(got[i].placeholder == expected[i].placeholder);
      }
      std::size_t stretches = 0;
      for (const MatchFinder::Located& located : again.locate(match)) {
        stretches += located.ranges.size();
      }
      CHECK(stretches == got.size());
    }
  }
}
