// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ContentInterpreter is the one walker of a page's content, docs/DESIGN.md
// chapter 6 and CLAUDE.md section 3.2: the graphics and text state machine of
// ISO 32000-1 chapters 8 and 9 (read) over the page's /Contents and,
// recursively, every form XObject it draws. It turns each shown code into a
// GlyphRecord, each group of path-painting operators into a FigureRecord and
// each image draw into an ImageOccurrence, and keeps every walk's bytes and
// operations so the serialiser can write them back. Extraction, the image
// listing and redaction all read its PageContent, because two readers of one
// stream disagree and an edit of bytes a second reader located is an edit of
// the wrong bytes.
//
// The rules it implements, each from docs/DESIGN.md section 6.1: the advance
// tx = ((w0 - adj/1000) * Tfs + Tc + Tw) * Th with Tw on a one-byte code 32
// alone (ISO 32000-1 section 9.4.4, read); a TJ number moves the position by
// -adj/1000 * Tfs * Th; vertical writing advances ty by w1; a form is walked
// with its /Matrix and its /BBox as a clip in a saved state, at most 32 deep,
// refused (with a note) when it is already on the path, and walked again each
// time it is drawn; patterns and soft masks are not walked, as MuPDF does not
// walk them (technique read in source, AGPL, no code taken); a Type 3 glyph is
// one record; an unknown operator is skipped with its operands; a damaged
// stream is read as far as it lexes. The interpreter never throws for a
// malformed page; only a refusal of the document itself (a decoding limit,
// the operator count) passes through.

#ifndef PDFKIT_TEXT_CONTENT_INTERPRETER_HPP
#define PDFKIT_TEXT_CONTENT_INTERPRETER_HPP

#include "fonts/font_loader.hpp"
#include "syntax/document.hpp"
#include "syntax/page.hpp"
#include "syntax/page_damage.hpp"
#include "text/optional_content.hpp"
#include "text/page_content.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace pdfkit::text {

class ContentInterpreter {
 public:
  // Forms nest at most this deep (docs/DESIGN.md section 6.1).
  static constexpr int max_form_depth = 32;

  // document, loader and optional_content must outlive the interpreter; the
  // loader's font cache is shared by every page interpreted with it.
  ContentInterpreter(syntax::Document& document, fonts::FontLoader& loader,
                     const OptionalContent& optional_content);

  // What a walk reads: everything, or only what names the page's damage
  // (every stream decoded and lexed, every form a Do draws followed, and no
  // record made), which costs a fraction of a full reading.
  enum class Reading : std::uint8_t { full, damage };

  // Interprets one page of the document.
  [[nodiscard]] PageContent run(const syntax::Page& page, Reading reading = Reading::full);

 private:
  // One page's walk, defined in the source file.
  class Walk;

  syntax::Document* document_;
  fonts::FontLoader* loader_;
  const OptionalContent* optional_content_;
  // A form stream decoded: its bytes, whether a filter met a fault, and what
  // the decoding left unread, as damaged streams of the page drawing it.
  struct FormBytes {
    std::shared_ptr<const std::vector<std::byte>> bytes;
    bool damaged = false;
    std::vector<syntax::PageDamage::Stream> damage;
  };

  // The decoded bytes of each form stream, by object number, so a form drawn
  // on many pages is decoded and charged to the budget once.
  std::map<std::uint32_t, FormBytes> forms_;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_CONTENT_INTERPRETER_HPP
