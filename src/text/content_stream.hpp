// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ContentStream is one walk of a content stream by the interpreter: the
// page's /Contents (its pieces joined with one newline, as the content lexer
// joins them) or one draw of a form XObject. A form drawn twice is walked
// twice and has two walks, each with its form path, because its glyphs land
// in two places; both walks share the form's bytes. The walk keeps the bytes,
// the operations the lexer read with their byte ranges, where each piece of
// the page's /Contents starts, and whether the bytes were read only up to a
// fault, which is everything the serialiser needs to write the stream back
// and a redaction needs to edit it.

#ifndef PDFKIT_TEXT_CONTENT_STREAM_HPP
#define PDFKIT_TEXT_CONTENT_STREAM_HPP

#include "syntax/content_lexer.hpp"
#include "syntax/object.hpp"
#include "text/matrix.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace pdfkit::text {

class ContentStream {
 public:
  // The form's object number; 0 for the page's own content.
  std::uint32_t form = 0;
  // The forms this walk was reached through, outermost first, ending with
  // this form; empty for the page's own content.
  std::vector<std::uint32_t> form_path;
  // The CTM the walk started under (the form's /Matrix included).
  Matrix matrix;
  // The decoded bytes (for the page, its pieces joined with one newline).
  std::shared_ptr<const std::vector<std::byte>> bytes;
  // Where each /Contents piece starts in bytes; one entry for a form.
  std::vector<std::size_t> piece_starts;
  std::vector<syntax::ContentLexer::Operation> operations;
  // The stream decoded only up to a fault, or the lexer recovered from a
  // fault inside it, so it is read as far as it reads.
  bool damaged = false;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_CONTENT_STREAM_HPP
