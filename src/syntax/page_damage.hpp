// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PageDamage names one page whose text could not be read in full, docs/
// DESIGN.md (the damaged pages section) and docs/ABI.md (damaged_pages): the
// glyphs read on it and, per content stream that lost text, which stream it
// is (a /Contents piece or a form with its form path), the damage class, the
// fault the decoder or the content lexer met and the bytes decoded and left
// unread. A page pdfkit repaired with no text lost is not one. Every output a
// caller reads writes these records in one shape through write(), so a
// caller never takes partial text for the whole page or "nothing found" for
// "nothing there"; pdfkit decides nothing with them.

#ifndef PDFKIT_SYNTAX_PAGE_DAMAGE_HPP
#define PDFKIT_SYNTAX_PAGE_DAMAGE_HPP

#include "platform/error.hpp"
#include "platform/json_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::syntax {

class PageDamage {
 public:
  // The class of a page whose reading met a limit, so whether it lost text
  // is not known.
  static constexpr std::string_view not_checked = "not_checked";

  struct Stream {
    // Whether the stream is known; a page not checked names none.
    bool known = true;
    // The /Contents piece (from 0) for the page's own content; no value for a
    // form.
    std::optional<std::size_t> piece;
    // The form's object number, 0 for the page's own content, and the forms
    // its walk was reached through, outermost first, ending with it.
    std::uint32_t form = 0;
    std::vector<std::uint32_t> form_path;
    // The damage class: a filter's (docs/DESIGN.md section 3.1), the content
    // lexer's (chapter 4) or not_checked.
    std::string damage;
    std::string fault;
    // The bytes the stream decoded to, and the bytes left unread when known:
    // a filter's input it did not read, or the content bytes a lexer loss
    // took from the drawing.
    std::size_t decoded_bytes = 0;
    std::optional<std::size_t> unread_bytes;

    friend bool operator==(const Stream&, const Stream&) = default;
  };

  // 1-based; for the write report, the output page.
  std::uint32_t page = 0;
  // The write report's source page.
  std::optional<std::uint32_t> source_page;
  // The glyphs read on the page, hidden ones included.
  std::size_t glyphs = 0;
  std::vector<Stream> streams;
  // The write report's own: whether a text redaction searched or edited the
  // page, and whether damaged bytes of the page reach the output as they
  // were.
  std::optional<bool> text_redaction;
  std::optional<bool> copied_as_is;

  // A page whose reading met the limit error names.
  [[nodiscard]] static PageDamage unchecked(std::uint32_t page, const platform::Error& error);

  // Adds a stream, or merges it into an entry of the same stream and class:
  // the first fault kept, the unread bytes summed while each is known.
  void add(Stream stream);

  // Writes the value of "damaged_pages": the pages in the order given.
  static void write(platform::JsonWriter& writer, const std::vector<PageDamage>& pages);
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_PAGE_DAMAGE_HPP
