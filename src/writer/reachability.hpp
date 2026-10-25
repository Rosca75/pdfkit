// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Reachability finds the objects the output holds, docs/DESIGN.md section 17:
// those reachable from the new trailer (/Root, then /Info) after every edit,
// and nothing else, because an unreferenced object is a channel (section 15).
// The walk is depth first and visits dictionary entries in their stored order
// and array items in order, so its first-reach order is the order the
// renumbering uses. It is iterative with an explicit stack and a visited set,
// so neither a deep graph nor a cycle can exhaust the stack or loop. A
// stream's /Length is not followed, because the writer writes the length as a
// direct integer. A reference to a missing object is counted once per number,
// and the serialiser writes it as null.

#ifndef PDFKIT_WRITER_REACHABILITY_HPP
#define PDFKIT_WRITER_REACHABILITY_HPP

#include "syntax/object.hpp"
#include "writer/object_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace pdfkit::writer {

class Reachability {
 public:
  // Walks graph from its trailer.
  explicit Reachability(ObjectGraph& graph);

  // The reachable object numbers in first-reach order.
  [[nodiscard]] const std::vector<std::uint32_t>& order() const noexcept { return order_; }
  // Whether number is reachable.
  [[nodiscard]] bool contains(std::uint32_t number) const noexcept {
    return reached_.contains(number);
  }
  // The distinct numbers referenced but missing.
  [[nodiscard]] std::size_t missing() const noexcept { return missing_.size(); }

  // The references in value in the walk's order (a stream's /Length left
  // out), appended to out.
  static void references(const syntax::Object& value,
                         std::vector<syntax::Object::Reference>& out);

 private:
  std::vector<std::uint32_t> order_;
  std::set<std::uint32_t> reached_;
  std::set<std::uint32_t> missing_;
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_REACHABILITY_HPP
