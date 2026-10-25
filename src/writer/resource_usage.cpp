// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The operator-to-category table of resource names. An operand of the wrong
// type names nothing, as a viewer ignores such an operator.

#include "writer/resource_usage.hpp"

#include "syntax/content_lexer.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <optional>
#include <utility>

namespace pdfkit::writer {

using syntax::Object;

namespace {

void add(ResourceUsage& usage, std::string_view category, const Object& operand) {
  if (const std::string* name = operand.name()) {
    usage.names[std::string(category)].insert(*name);
  }
}

}  // namespace

ResourceUsage ResourceUsage::of(const std::vector<std::vector<std::byte>>& pieces,
                                const syntax::Limits& limits, std::string what) {
  ResourceUsage usage;
  syntax::ContentLexer lexer(pieces, limits, std::move(what));
  while (const std::optional<syntax::ContentLexer::Operation> operation = lexer.next()) {
    const std::string& op = operation->op;
    const std::vector<Object>& operands = operation->operands;
    if (operands.empty()) {
      continue;
    }
    if (op == "Tf") {
      add(usage, "Font", operands.front());
    } else if (op == "Do") {
      add(usage, "XObject", operands.front());
    } else if (op == "gs") {
      add(usage, "ExtGState", operands.front());
    } else if (op == "cs" || op == "CS") {
      add(usage, "ColorSpace", operands.front());
    } else if (op == "scn" || op == "SCN") {
      add(usage, "Pattern", operands.back());
    } else if (op == "sh") {
      add(usage, "Shading", operands.front());
    } else if ((op == "BDC" || op == "DP") && operands.size() >= 2) {
      add(usage, "Properties", operands[1]);
    } else if (op == "BI") {
      if (const syntax::Dictionary* dictionary = operands.front().dictionary()) {
        add(usage, "ColorSpace", dictionary->get("ColorSpace"));
        add(usage, "ColorSpace", dictionary->get("CS"));
      }
    }
  }
  usage.complete = lexer.faults() == 0;
  return usage;
}

void ResourceUsage::merge(const ResourceUsage& other) {
  for (const auto& [category, set] : other.names) {
    names[category].insert(set.begin(), set.end());
  }
  complete = complete && other.complete;
}

bool ResourceUsage::uses(std::string_view category, std::string_view name) const {
  const auto found = names.find(category);
  return found != names.end() && found->second.contains(std::string(name));
}

}  // namespace pdfkit::writer
