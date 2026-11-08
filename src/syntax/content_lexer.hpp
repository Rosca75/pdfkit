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
// operation count is bounded by the open option operators_per_page. Every
// recovery is counted, and those that can take text are kept as losses,
// which name the page as damaged (docs/DESIGN.md, the damaged pages section).

#ifndef PDFKIT_SYNTAX_CONTENT_LEXER_HPP
#define PDFKIT_SYNTAX_CONTENT_LEXER_HPP

#include "platform/byte_span.hpp"
#include "syntax/lexer.hpp"
#include "syntax/limits.hpp"
#include "syntax/object.hpp"
#include "syntax/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

  // The recoveries that can take text from the page (docs/DESIGN.md chapter
  // 4), one entry per class and /Contents piece, so their memory is bounded
  // by the classes whatever the content holds: bytes the lexer read as no
  // operation, or an operation it could not read whole. A recovery that
  // skips no byte and drops no operator (operands left before BI or at the
  // end, a stray delimiter, a token read leniently, a reference written in
  // content, an unknown operator with no operand or numbers alone) is no
  // loss.
  struct Loss {
    // unknown_operator: a keyword Annex A does not know, outside BX and EX,
    // with an operand that is not a number;
    // unlexable_content: an operator read as an operand's value, a
    // dictionary key that is not a name, containers nested past the limit,
    // operands past their bound, a string that runs to the end;
    // inline_image: an inline image whose dictionary or end is not read.
    std::string damage;
    // The first loss's message.
    std::string fault;
    // Where the first loss starts in the joined bytes.
    std::size_t start = 0;
    // The bytes the losses take from the drawing, summed, when each is known.
    std::optional<std::size_t> bytes;
    // The losses of this class in this piece.
    std::size_t count = 0;
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
  // The losses met so far, one per class and piece, in the order first met.
  [[nodiscard]] const std::vector<Loss>& losses() const noexcept { return losses_; }

  // Whether a keyword is one of the content operators of ISO 32000-1 Annex A
  // (read).
  [[nodiscard]] static bool is_operator(std::string_view keyword) noexcept;

 private:
  [[nodiscard]] Operation inline_image(std::size_t start, std::size_t operator_start);
  [[nodiscard]] std::optional<std::size_t> computed_length(const Object& dictionary) const;
  [[nodiscard]] bool plausible_after(std::size_t position) const;
  // Counts a loss of class damage starting at start into the entry of its
  // class and piece; true when it opened that entry, whose fault the caller
  // then writes, so a message is formatted once per entry.
  bool lose(std::string_view damage, std::size_t start, std::optional<std::size_t> bytes);
  // Records the recoveries of one operand's parse that can take text.
  void lose_recoveries(const std::vector<Parser::Recovery>& recoveries, std::size_t start);
  // Whether an operator Annex A does not know could have drawn nothing.
  [[nodiscard]] static bool draws_nothing(const Operation& operation);

  std::vector<std::byte> joined_;
  std::vector<std::size_t> piece_starts_;
  Lexer lexer_;
  const Limits* limits_;
  std::string what_;
  std::uint64_t count_ = 0;
  std::size_t faults_ = 0;
  std::vector<Loss> losses_;
  // The BX sections open, inside which an unknown operator is no loss
  // (ISO 32000-1 section 7.8.2, recalled).
  std::size_t compatibility_ = 0;
  bool string_lost_ = false;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_CONTENT_LEXER_HPP
