// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// OptionalContentRemover takes out of the output what the document's default
// optional-content configuration turns off, docs/DESIGN.md section 15: a
// marked-content sequence /OC ... BDC ... EMC whose group or membership
// dictionary is off draws nothing, and a Do of a form or image XObject whose
// /OC is off is removed, because a viewer hides that content and an extractor
// reads it, and the channel table then drops /OCProperties and every /OC key,
// which would show it. Inside an off sequence the operators that change the
// graphics state stay (ISO 32000-1 section 8.11.3.2, recalled: hidden content
// is not drawn but the state it sets is), so what follows the sequence draws as
// it did: q and Q, cm, colours, the text state and the text positioning are
// kept; a path that paints is removed with its construction, or keeps its
// construction ended by n when it also clips; a text-showing operator becomes
// a TJ of one number moving the text position by the advance its glyphs had;
// Do, sh, inline images and marked-content operators go. The /OC marks of a
// sequence that is on are removed and its content kept. The removed
// XObjects and the /Properties entries naming groups leave every resource
// dictionary walked, so the writer's reachable sweep drops them.
//
// It runs after every other edit of the content and before the channel
// strip, on the content as the earlier stages left it: each stream is lexed
// again from the object graph and written back through
// text::ContentSerialiser, which copies every byte it is not told to change.
// The walk covers the pages, the forms they draw and the tiling patterns and
// soft-mask groups their resources name, as redact::ImageRemover does.

#ifndef PDFKIT_REDACT_OPTIONAL_CONTENT_REMOVER_HPP
#define PDFKIT_REDACT_OPTIONAL_CONTENT_REMOVER_HPP

#include "writer/object_graph.hpp"

#include <cstddef>

namespace pdfkit::redact {

class OptionalContentRemover {
 public:
  struct Result {
    // The off sequences and off XObject draws removed.
    std::size_t sequences = 0;
    std::size_t draws = 0;
    // The content streams written again (page contents and forms).
    std::size_t streams_rewritten = 0;
  };

  // Removes the off content from every page of graph's page tree; in a
  // document without /OCProperties every group is on, so only the marks go.
  static Result apply(writer::ObjectGraph& graph);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_OPTIONAL_CONTENT_REMOVER_HPP
