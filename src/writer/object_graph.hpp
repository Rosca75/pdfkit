// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ObjectGraph is the output of a write while it is being built: the source
// document's objects seen through the edits every stage of the write makes
// (an object replaced under its own number, an object added under a number
// past the source's), and the new trailer, which names only /Root and /Info.
// The source document is never changed: its objects are immutable and shared,
// so an edit is a new object under the old number, and everything a stage did
// not touch is read from the document and later copied byte for byte. Only
// what is reachable from the new trailer is written (docs/DESIGN.md section
// 17), so an edit that drops a reference drops what only it reached.

#ifndef PDFKIT_WRITER_OBJECT_GRAPH_HPP
#define PDFKIT_WRITER_OBJECT_GRAPH_HPP

#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"

#include <cstdint>
#include <map>
#include <string_view>

namespace pdfkit::writer {

class ObjectGraph {
 public:
  // References resolve through at most this many hops, as the document's do.
  static constexpr int max_reference_chain = 32;

  // document must outlive the graph. The trailer starts as the source's /Root
  // and /Info; a direct catalog or information dictionary is given a number.
  explicit ObjectGraph(syntax::Document& document);

  [[nodiscard]] syntax::Document& document() noexcept { return *document_; }

  // The object numbered number with the edits applied; null when it is
  // missing.
  [[nodiscard]] syntax::Object get(std::uint32_t number);
  // object with its references followed; any other object as it is.
  [[nodiscard]] syntax::Object resolve(const syntax::Object& object);
  [[nodiscard]] syntax::Object resolve_entry(const syntax::Dictionary& dictionary,
                                             std::string_view key);

  // Replaces the object numbered number.
  void set(std::uint32_t number, syntax::Object value);
  // Adds an object under a fresh number and returns its reference.
  [[nodiscard]] syntax::Object::Reference add(syntax::Object value);
  // Whether number was replaced or added by an edit.
  [[nodiscard]] bool edited(std::uint32_t number) const noexcept;

  // The new trailer: /Root and, when kept, /Info, both references.
  [[nodiscard]] const syntax::Dictionary& trailer() const noexcept { return trailer_; }
  void set_trailer(syntax::Dictionary trailer) { trailer_ = std::move(trailer); }
  // The number of the catalog, or 0 when the source has none.
  [[nodiscard]] std::uint32_t root() const noexcept;

 private:
  syntax::Document* document_;
  std::map<std::uint32_t, syntax::Object> edits_;
  std::uint32_t next_ = 1;
  syntax::Dictionary trailer_;
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_OBJECT_GRAPH_HPP
