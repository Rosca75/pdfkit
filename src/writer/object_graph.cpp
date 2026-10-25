// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The edits over the source document and the new trailer. Fresh numbers start
// past the largest number the source's table names, so an added object never
// shadows a source object, whether or not that object is ever written.

#include "writer/object_graph.hpp"

#include "platform/error.hpp"

#include <utility>

namespace pdfkit::writer {

using syntax::Dictionary;
using syntax::Object;

ObjectGraph::ObjectGraph(syntax::Document& document) : document_(&document) {
  if (!document.entries().empty()) {
    next_ = document.entries().rbegin()->first + 1;
  }
  for (const std::string_view key : {"Root", "Info"}) {
    const Object& value = document.trailer().get(key);
    if (const auto reference = value.reference()) {
      if (reference->number < next_) {
        trailer_.set(std::string(key), Object::make_reference({reference->number, 0}));
      }
    } else if (value.dictionary() != nullptr) {
      trailer_.set(std::string(key), Object::make_reference(add(value)));
    }
  }
}

Object ObjectGraph::get(std::uint32_t number) {
  if (const auto found = edits_.find(number); found != edits_.end()) {
    return found->second;
  }
  if (number >= next_ || number == 0) {
    return {};
  }
  return document_->object({number, 0});
}

Object ObjectGraph::resolve(const Object& object) {
  Object current = object;
  for (int hop = 0; hop < max_reference_chain; ++hop) {
    const auto reference = current.reference();
    if (!reference) {
      return current;
    }
    current = get(reference->number);
  }
  return {};
}

Object ObjectGraph::resolve_entry(const Dictionary& dictionary, std::string_view key) {
  return resolve(dictionary.get(key));
}

void ObjectGraph::set(std::uint32_t number, Object value) {
  if (number == 0 || number >= next_) {
    throw platform::Error(platform::ErrorCode::internal,
                          "the writer replaced object " + std::to_string(number) +
                              ", which the graph never numbered",
                          "an edit of a source object or of an object the graph added",
                          "this is a defect in pdfkit; report it with the input file");
  }
  edits_[number] = std::move(value);
}

Object::Reference ObjectGraph::add(Object value) {
  const std::uint32_t number = next_++;
  edits_[number] = std::move(value);
  return {number, 0};
}

bool ObjectGraph::edited(std::uint32_t number) const noexcept {
  return edits_.contains(number);
}

std::uint32_t ObjectGraph::root() const noexcept {
  const auto reference = trailer_.get("Root").reference();
  return reference ? reference->number : 0;
}

}  // namespace pdfkit::writer
