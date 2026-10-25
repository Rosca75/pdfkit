// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The default configuration's state and the evaluation of membership
// dictionaries. A visibility expression nests at most 32 deep; deeper, it
// reads as visible, because hiding text on a guess would take it out of the
// visible reading.

#include "text/optional_content.hpp"

#include "syntax/dictionary.hpp"

#include <string>

namespace pdfkit::text {

namespace {

constexpr int max_expression_depth = 32;

}  // namespace

OptionalContent::OptionalContent(syntax::Document& document) : document_(&document) {
  const syntax::Object catalog = document.catalog();
  const syntax::Dictionary* root = catalog.dictionary();
  if (root == nullptr) {
    return;
  }
  const syntax::Object properties = document.resolve_entry(*root, "OCProperties");
  const syntax::Dictionary* entries = properties.dictionary();
  if (entries == nullptr) {
    return;
  }
  present_ = true;
  const syntax::Object configuration = document.resolve_entry(*entries, "D");
  const syntax::Dictionary* d = configuration.dictionary();
  if (d == nullptr) {
    return;
  }
  base_off_ = document.resolve_entry(*d, "BaseState").is_name("OFF");
  const auto collect = [&](std::string_view key, std::set<std::uint32_t>& into) {
    const syntax::Object list = document.resolve_entry(*d, key);
    if (const syntax::Object::Array* items = list.array()) {
      for (const syntax::Object& item : *items) {
        if (const auto reference = item.reference()) {
          into.insert(reference->number);
        }
      }
    }
  };
  collect("ON", on_);
  collect("OFF", off_);
}

bool OptionalContent::group_off(const syntax::Object& value) const {
  const auto reference = value.reference();
  if (!reference) {
    return false;
  }
  if (off_.contains(reference->number)) {
    return true;
  }
  if (on_.contains(reference->number)) {
    return false;
  }
  return base_off_;
}

bool OptionalContent::expression_off(const syntax::Object& value, int depth) const {
  if (depth > max_expression_depth) {
    return false;
  }
  const syntax::Object resolved = document_->resolve(value);
  const syntax::Object::Array* items = resolved.array();
  if (items == nullptr || items->empty()) {
    return group_off(value);
  }
  const std::string* op = items->front().name();
  if (op == nullptr) {
    return false;
  }
  if (*op == "Not") {
    return items->size() > 1 && !expression_off((*items)[1], depth + 1);
  }
  const bool all = *op == "And";
  if (!all && *op != "Or") {
    return false;
  }
  // And is visible when every operand is; Or when any is.
  bool any_visible = false;
  bool all_visible = true;
  for (std::size_t i = 1; i < items->size(); ++i) {
    const bool visible = !expression_off((*items)[i], depth + 1);
    any_visible = any_visible || visible;
    all_visible = all_visible && visible;
  }
  return all ? !all_visible : !any_visible;
}

bool OptionalContent::hidden(const syntax::Object& value) const {
  if (!present_) {
    return false;
  }
  const syntax::Object resolved = document_->resolve(value);
  const syntax::Dictionary* entries = resolved.dictionary();
  if (entries == nullptr) {
    return false;
  }
  const bool membership = document_->resolve_entry(*entries, "Type").is_name("OCMD") ||
                          entries->contains("OCGs") || entries->contains("VE");
  if (!membership) {
    return group_off(value);
  }
  if (const syntax::Object* expression = entries->find("VE")) {
    return expression_off(*expression, 0);
  }
  const syntax::Object* groups_value = entries->find("OCGs");
  if (groups_value == nullptr) {
    return false;
  }
  std::vector<syntax::Object> groups;
  const syntax::Object groups_resolved = document_->resolve(*groups_value);
  if (const syntax::Object::Array* list = groups_resolved.array()) {
    groups = *list;
  } else {
    groups.push_back(*groups_value);
  }
  if (groups.empty()) {
    return false;
  }
  std::size_t off = 0;
  for (const syntax::Object& group : groups) {
    off += group_off(group) ? 1U : 0U;
  }
  const syntax::Object policy = document_->resolve_entry(*entries, "P");
  if (policy.is_name("AllOn")) {
    return off > 0;
  }
  if (policy.is_name("AnyOff")) {
    return off == 0;
  }
  if (policy.is_name("AllOff")) {
    return off != groups.size();
  }
  return off == groups.size();
}

}  // namespace pdfkit::text
