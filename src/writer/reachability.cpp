// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The walk. An object's references are pushed in reverse so the first one is
// taken first, which gives the order a recursive depth-first walk would give
// without recursing on file content.

#include "writer/reachability.hpp"

#include "syntax/dictionary.hpp"
#include "syntax/stream.hpp"

#include <ranges>
#include <utility>

namespace pdfkit::writer {

using syntax::Object;

namespace {

// One direct value still to scan for references: the walk over a value's own
// containers is iterative too.
void collect(const Object& value, std::vector<Object::Reference>& out) {
  std::vector<const Object*> pending{&value};
  // The containers are scanned depth first in order: children are pushed in
  // reverse.
  while (!pending.empty()) {
    const Object* current = pending.back();
    pending.pop_back();
    switch (current->kind()) {
      case Object::Kind::reference:
        if (const auto reference = current->reference()) {
          out.push_back(*reference);
        }
        break;
      case Object::Kind::array:
        for (const Object& item : std::views::reverse(*current->array())) {
          pending.push_back(&item);
        }
        break;
      case Object::Kind::dictionary:
        for (const auto& entry : std::views::reverse(current->dictionary()->entries())) {
          pending.push_back(&entry.second);
        }
        break;
      case Object::Kind::stream:
        for (const auto& entry : std::views::reverse(current->stream()->dictionary().entries())) {
          if (entry.first != "Length") {
            pending.push_back(&entry.second);
          }
        }
        break;
      default:
        break;
    }
  }
}

}  // namespace

void Reachability::references(const Object& value, std::vector<Object::Reference>& out) {
  collect(value, out);
}

Reachability::Reachability(ObjectGraph& graph) {
  std::vector<std::uint32_t> stack;
  for (const std::string_view key : {"Info", "Root"}) {
    if (const auto reference = graph.trailer().get(key).reference()) {
      stack.push_back(reference->number);
    }
  }
  std::vector<Object::Reference> found;
  while (!stack.empty()) {
    const std::uint32_t number = stack.back();
    stack.pop_back();
    if (reached_.contains(number) || missing_.contains(number)) {
      continue;
    }
    const Object value = graph.get(number);
    if (value.is_null()) {
      missing_.insert(number);
      continue;
    }
    reached_.insert(number);
    order_.push_back(number);
    found.clear();
    collect(value, found);
    for (const Object::Reference reference : std::views::reverse(found)) {
      if (!reached_.contains(reference.number)) {
        stack.push_back(reference.number);
      }
    }
  }
}

}  // namespace pdfkit::writer
