// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ContentLexer tokenises a content stream into operations, docs/DESIGN.md
// chapter 4: operands in the object syntax without references and the
// operator that ends them, each operation with its byte range, over a page's
// /Contents pieces joined with one newline between them, because ISO 32000-1
// section 7.8.2 (read) lets the pieces split only at token boundaries and an
// operator may span two pieces. A range maps back to its piece and the offset
// inside it. An inline image "BI ... ID data EI" is one operation whose data
// ends at the length its dictionary makes computable (section 8.9.7, read),
// else at the first "EI" preceded by white space and followed by white space
// or the end, whose following bytes lex as operators: PdfPig's check against
// a false EI inside binary data (PR 1476, read in the design record). The
// operation count is bounded by the open option operators_per_page.

#ifndef PDFKIT_SYNTAX_CONTENT_LEXER_HPP
#define PDFKIT_SYNTAX_CONTENT_LEXER_HPP

#include "platform/byte_span.hpp"
#include "syntax/lexer.hpp"
#include "syntax/limits.hpp"
#include "syntax/object.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::syntax {

class ContentLexer {
 public:
  struct Operation {
    // The operator's text: "Tj", "cm", "BI" for a whole inline image.
    std::string op;
    std::vector<Object> operands;
    // [start, end) covers the operands and the operator (for an inline image,
    // from BI to the end of EI).
    std::size_t start = 0;
    std::size_t end = 0;
    // Where the operator itself starts.
    std::size_t operator_start = 0;
    // An inline image's data, [data_start, data_end); operands[0] is its
    // dictionary.
    std::size_t data_start = 0;
    std::size_t data_end = 0;
  };

  struct Location {
    std::size_t piece = 0;
    std::size_t offset = 0;
  };

  // pieces are a /Contents array's decoded streams in order (one for a form or
  // a single stream); what names the stream in a refusal ("page 3").
  ContentLexer(const std::vector<std::vector<std::byte>>& pieces, const Limits& limits,
               std::string what);

  // The lexer reads the joined bytes this object owns, so it is not copied;
  // a move keeps the bytes where they are.
  ContentLexer(const ContentLexer&) = delete;
  ContentLexer& operator=(const ContentLexer&) = delete;
  ContentLexer(ContentLexer&&) noexcept = default;
  ContentLexer& operator=(ContentLexer&&) noexcept = default;
  ~ContentLexer() = default;

  // The next operation, or no value at the end of the content.
  [[nodiscard]] std::optional<Operation> next();

  // The piece and the offset inside it of a byte of the joined content; a
  // joining newline maps to the end of the piece before it.
  [[nodiscard]] Location locate(std::size_t offset) const noexcept;

  [[nodiscard]] platform::ByteSpan bytes() const noexcept { return joined_; }
  // Recoveries met so far: a stray delimiter, an unterminated string, an
  // inline image without EI.
  [[nodiscard]] std::size_t faults() const noexcept { return faults_; }

  // Whether a keyword is one of the content operators of ISO 32000-1 Annex A
  // (read).
  [[nodiscard]] static bool is_operator(std::string_view keyword) noexcept;

 private:
  [[nodiscard]] Operation inline_image(std::size_t start, std::size_t operator_start);
  [[nodiscard]] std::optional<std::size_t> computed_length(const Object& dictionary) const;
  [[nodiscard]] bool plausible_after(std::size_t position) const;

  std::vector<std::byte> joined_;
  std::vector<std::size_t> piece_starts_;
  Lexer lexer_;
  const Limits* limits_;
  std::string what_;
  std::uint64_t count_ = 0;
  std::size_t faults_ = 0;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_CONTENT_LEXER_HPP
