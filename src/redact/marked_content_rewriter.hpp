// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// MarkedContentRewriter keeps a redacted string from surviving as a property
// of marked content, docs/DESIGN.md section 12: an /ActualText, /Alt or /E
// value (ISO 32000-1 section 14.9, read) is what an extractor copies instead
// of the glyphs, so on a span whose glyphs are all removed the /ActualText
// goes with them (the placeholder drawn for them is then read once, inside
// or beside the span) and /Alt and /E become the placeholder, and on any
// other span every occurrence of a redacted string inside a value (under the
// string's folding) is replaced by its placeholder. Every other string of a
// property list, at any depth (an artifact's /Contents, a producer's private
// key), has the redacted strings replaced too, because an extractor can copy
// it as well. A value is a PDF text string; it is read as
// UTF-8 and written back as PDFDocEncoding-compatible ASCII when it is ASCII and as UTF-16BE with a
// byte order mark otherwise (ISO 32000-1 section 7.9.2.2, read). The property list is rewritten
// where it lives: inline, the BDC operation is written again around the new dictionary; by name,
// the dictionary the /Properties resource names is replaced.

#ifndef PDFKIT_REDACT_MARKED_CONTENT_REWRITER_HPP
#define PDFKIT_REDACT_MARKED_CONTENT_REWRITER_HPP

#include "redact/match_finder.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "text/content_stream.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pdfkit::redact {

class MarkedContentRewriter {
 public:
  // The property keys whose text an extractor copies.
  static constexpr std::array<std::string_view, 3> keys{"ActualText", "Alt", "E"};

  // The index of the EMC closing the marked content opened by operation
  // open of stream; the number of operations when it is never closed.
  [[nodiscard]] static std::size_t closing(const text::ContentStream& stream, std::size_t open);

  // value (UTF-8) with every occurrence of a needle replaced by the needle's
  // placeholder; no value when no needle occurs.
  [[nodiscard]] static std::optional<std::string> replace(
      std::string_view value, std::span<const MatchFinder::Match> needles);

  // properties with its text values rewritten: when whole is given (the
  // span's glyphs are all removed) /ActualText removed and /Alt and /E set to
  // *whole, else every value with the needles replaced. No value when nothing
  // changes. resolve reads a value given by reference.
  [[nodiscard]] static std::optional<syntax::Dictionary> rewrite(
      const syntax::Dictionary& properties, std::span<const MatchFinder::Match> needles,
      const std::string* whole,
      const std::function<syntax::Object(const syntax::Object&)>& resolve);

  // The bytes of a BDC operation whose operands are tag and the inline
  // properties.
  [[nodiscard]] static std::string operation(const syntax::Object& tag,
                                             const syntax::Dictionary& properties,
                                             std::string_view op = "BDC");

  // value with every occurrence of a needle in its strings (inside arrays
  // and dictionaries too, at most 32 deep) replaced; changed says whether one
  // was.
  [[nodiscard]] static syntax::Object replace_strings(const syntax::Object& value,
                                                      std::span<const MatchFinder::Match> needles,
                                                      int depth, bool& changed);

  // utf8 as a PDF text string object.
  [[nodiscard]] static syntax::Object text_string(std::string_view utf8);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_MARKED_CONTENT_REWRITER_HPP
