// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageRemover edits the operators that draw images, docs/DESIGN.md section
// 14: every Do of a removed image XObject goes from every content stream that
// draws it (the page's contents, the forms it draws, the tiling patterns and
// soft-mask groups its resources name, and theirs in turn), the operator alone
// with its operand, so the q, Q and cm around it stay and the graphics state
// stays balanced; a removed inline image's whole BI ... ID ... EI goes; and the
// removed XObject's entry goes from every resource dictionary walked, so
// nothing reaches the object and the writer's reachable sweep drops it. The
// same walk replaces a treated inline image's BI ... EI with the bytes its
// treatment wrote, because an inline image lives in the content stream that
// draws it. It runs after the text surgery, on the content as that stage left
// it: each stream is lexed again from the object graph (the same lexer gives
// the same operations) and written back through text::ContentSerialiser,
// which copies every byte it is not told to change. A form is written once,
// whatever draws it; a form without resources of its own is walked with the
// resources of each page that draws it.

#ifndef PDFKIT_REDACT_IMAGE_REMOVER_HPP
#define PDFKIT_REDACT_IMAGE_REMOVER_HPP

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "writer/object_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pdfkit::redact {

class ImageRemover {
 public:
  // The bytes that replace a treated inline image's whole operation, given
  // its identity, its dictionary as written, its data and the /ColorSpace
  // resources of the content drawing it; no value keeps it as it is.
  using InlineEdit = std::function<std::optional<std::string>(
      const std::string& identity, const syntax::Dictionary& dictionary, platform::ByteSpan data,
      const syntax::Object& colour_spaces)>;

  struct Request {
    // The image XObjects to remove: object number to identity.
    std::map<std::uint32_t, std::string> objects;
    // The inline images to remove, by identity.
    std::set<std::string> inline_identities;
    // The inline identities a treatment rewrites, and how.
    std::set<std::string> inline_treated;
    InlineEdit edit;
    // The output pages to walk, by index from 0; every page when absent.
    std::optional<std::set<std::size_t>> pages;
  };

  struct Result {
    // Per identity, the draws removed (Do and BI operations) and the inline
    // draws replaced.
    std::map<std::string, std::size_t> removed;
    std::map<std::string, std::size_t> replaced;
    // The content streams written again (page contents and forms).
    std::size_t streams_rewritten = 0;
  };

  // Walks the pages whose dictionaries are the objects page_objects (the
  // output's pages, in order) and applies request.
  static Result apply(writer::ObjectGraph& graph, const std::vector<std::uint32_t>& page_objects,
                      const Request& request);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_IMAGE_REMOVER_HPP
