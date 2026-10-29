// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The strings of marked-content property lists. A named list resolves
// through the /Properties of the resources its walk reads: the innermost
// form on its path with resources of its own, else the page's (ISO 32000-1
// section 14.6.2, read), as the content interpreter reads it. A reference
// inside a list is read through, at most 32 deep and never back into an
// object on its own path, and a list's walk stops after a fixed number of
// items, so a list built as a web of shared references costs a bounded
// walk. The operators that name a list are counted over the pages' content
// and the forms they draw, each form once per draw, as FormSplitter counts
// draws, because the interpreter reads one walk per draw.

#include "redact/property_strings.hpp"

#include "platform/unicode/folding.hpp"
#include "redact/marked_content_rewriter.hpp"
#include "syntax/content_lexer.hpp"
#include "text/text_string.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace pdfkit::redact {

namespace {

using syntax::Dictionary;
using syntax::Object;
using TextFolding = platform::unicode::Folding;

// As deep as the interpreter walks forms and the rewriter walks a value.
constexpr int max_depth = 32;
// The items one list's walk reads at most.
constexpr std::size_t max_items = 65536;

[[nodiscard]] PropertyStrings::Surface surface_of(std::string_view key) noexcept {
  if (key == "Alt") {
    return PropertyStrings::Surface::alt;
  }
  if (key == "E") {
    return PropertyStrings::Surface::expansion;
  }
  if (key == "ActualText") {
    return PropertyStrings::Surface::actual_text;
  }
  return PropertyStrings::Surface::property;
}

// The resources a walk reads and the form that holds them (0 for the
// page's).
[[nodiscard]] std::pair<Object, std::uint32_t> resources_of(syntax::Document& document,
                                                            const syntax::Page& page,
                                                            const text::ContentStream& stream) {
  for (const std::uint32_t number : std::views::reverse(stream.form_path)) {
    const Object form = document.object({number, 0});
    if (const syntax::Stream* object = form.stream()) {
      const Object own = document.resolve_entry(object->dictionary(), "Resources");
      if (own.dictionary() != nullptr) {
        return {own, number};
      }
    }
  }
  return {document.resolve(page.resources), 0};
}

// The /Properties dictionary of resources, or null.
[[nodiscard]] Object properties_of(syntax::Document& document, const Object& resources) {
  const Object resolved = document.resolve(resources);
  if (const Dictionary* entries = resolved.dictionary()) {
    return document.resolve_entry(*entries, "Properties");
  }
  return {};
}

// The identity of the list name names in properties, read in holder's
// resources on page; no value when it names no dictionary.
[[nodiscard]] std::optional<std::pair<PropertyStrings::Identity, Object>> named(
    syntax::Document& document, const Object& properties, const std::string& name,
    std::uint32_t holder, std::uint32_t page) {
  const Dictionary* entries = properties.dictionary();
  const Object* raw = entries != nullptr ? entries->find(name) : nullptr;
  if (raw == nullptr) {
    return std::nullopt;
  }
  Object value = document.resolve(*raw);
  if (value.dictionary() == nullptr) {
    return std::nullopt;
  }
  PropertyStrings::Identity identity;
  if (const auto reference = raw->reference()) {
    identity.object = reference->number;
  } else {
    identity.holder = holder;
    identity.page = holder == 0 ? page : 0;
    identity.name = name;
  }
  return std::make_pair(std::move(identity), std::move(value));
}

class StringWalk {
 public:
  StringWalk(syntax::Document& document, std::vector<PropertyStrings::String>& out)
      : document_(document), out_(out) {}

  void walk(const Object& value, PropertyStrings::Path& path, std::size_t list,
            PropertyStrings::Surface surface, int depth) {
    if (depth > max_depth || ++items_ > max_items) {
      return;
    }
    if (const auto reference = value.reference()) {
      if (std::ranges::find(stack_, reference->number) != stack_.end()) {
        return;
      }
      stack_.push_back(reference->number);
      walk(document_.resolve(value), path, list, surface, depth + 1);
      stack_.pop_back();
      return;
    }
    if (const Object::String* string = value.string()) {
      out_.push_back({list, surface, path, text::TextString::to_utf8(string->bytes)});
      return;
    }
    if (const Object::Array* items = value.array()) {
      for (std::size_t i = 0; i < items->size(); ++i) {
        path.emplace_back(i);
        walk((*items)[i], path, list, surface, depth + 1);
        path.pop_back();
      }
      return;
    }
    if (const Dictionary* entries = value.dictionary()) {
      for (const auto& [key, item] : entries->entries()) {
        path.emplace_back(key);
        walk(item, path, list, surface, depth + 1);
        path.pop_back();
      }
    }
  }

  void reset() {
    items_ = 0;
    stack_.clear();
  }

 private:
  syntax::Document& document_;
  std::vector<PropertyStrings::String>& out_;
  std::size_t items_ = 0;
  std::vector<std::uint32_t> stack_;
};

// Counts the operators naming each list through /Properties, over one
// draw of some content and the forms it draws.
class UseCounter {
 public:
  explicit UseCounter(syntax::Document& document) : document_(document) {}

  using Counts = std::map<PropertyStrings::Identity, std::size_t>;

  void count(const std::vector<std::vector<std::byte>>& pieces, const Object& resources,
             std::uint32_t holder, std::uint32_t page, int depth, std::vector<std::uint32_t>& path,
             Counts& out) {
    if (depth > max_depth) {
      return;
    }
    syntax::ContentLexer lexer(pieces, document_.limits(), "a property list count");
    const Object resolved = document_.resolve(resources);
    const Dictionary* entries = resolved.dictionary();
    const Object xobjects =
        entries != nullptr ? document_.resolve_entry(*entries, "XObject") : Object();
    const Object properties = properties_of(document_, resolved);
    while (const std::optional<syntax::ContentLexer::Operation> operation = lexer.next()) {
      const std::string& op = operation->op;
      if ((op == "BDC" || op == "DP") && operation->operands.size() >= 2) {
        if (const std::string* name = operation->operands[1].name()) {
          if (const auto found = named(document_, properties, *name, holder, page)) {
            ++out[found->first];
          }
        }
        continue;
      }
      if (op != "Do" || operation->operands.empty()) {
        continue;
      }
      const std::string* name = operation->operands.front().name();
      const Dictionary* names = xobjects.dictionary();
      const Object* raw = name != nullptr && names != nullptr ? names->find(*name) : nullptr;
      const std::optional<Object::Reference> reference =
          raw != nullptr ? raw->reference() : std::nullopt;
      if (!reference || std::ranges::find(path, reference->number) != path.end()) {
        continue;
      }
      const Object value = document_.resolve(*raw);
      const syntax::Stream* stream = value.stream();
      if (stream == nullptr ||
          !document_.resolve_entry(stream->dictionary(), "Subtype").is_name("Form")) {
        continue;
      }
      form(reference->number, *stream, resources, holder, page, depth, path, out);
    }
  }

 private:
  void form(std::uint32_t number, const syntax::Stream& stream, const Object& outer,
            std::uint32_t holder, std::uint32_t page, int depth, std::vector<std::uint32_t>& path,
            Counts& out) {
    const Object own = document_.resolve_entry(stream.dictionary(), "Resources");
    const bool has_own = own.dictionary() != nullptr;
    if (has_own) {
      // What a form with resources of its own names does not depend on who
      // draws it: counted once, added per draw.
      auto found = memo_.find(number);
      if (found == memo_.end()) {
        Counts inner;
        const std::vector<std::vector<std::byte>> pieces{document_.decode(stream).bytes};
        path.push_back(number);
        count(pieces, own, number, 0, depth + 1, path, inner);
        path.pop_back();
        found = memo_.emplace(number, std::move(inner)).first;
      }
      for (const auto& [identity, uses] : found->second) {
        out[identity] += uses;
      }
      return;
    }
    const std::vector<std::vector<std::byte>> pieces{document_.decode(stream).bytes};
    path.push_back(number);
    count(pieces, outer, holder, page, depth + 1, path, out);
    path.pop_back();
  }

  syntax::Document& document_;
  std::map<std::uint32_t, Counts> memo_;
};

// value with the edits of one path applied to the string at path[at...].
[[nodiscard]] Object replace_at(syntax::Document& document, const Object& value,
                                const PropertyStrings::Path& path, std::size_t at,
                                std::span<const PropertyStrings::Edit> edits, int depth,
                                bool& changed) {
  if (depth > max_depth) {
    return value;
  }
  const Object resolved = value.reference() ? document.resolve(value) : value;
  if (at == path.size()) {
    const Object::String* string = resolved.string();
    if (string == nullptr) {
      return value;
    }
    const std::u32string text = TextFolding::decode(text::TextString::to_utf8(string->bytes));
    changed = true;
    return MarkedContentRewriter::text_string(
        TextFolding::encode(PropertyStrings::edited(text, {edits.begin(), edits.end()})));
  }
  if (const std::string* key = std::get_if<std::string>(&path[at])) {
    const Dictionary* entries = resolved.dictionary();
    const Object* item = entries != nullptr ? entries->find(*key) : nullptr;
    if (item == nullptr) {
      return value;
    }
    bool here = false;
    Object replaced = replace_at(document, *item, path, at + 1, edits, depth + 1, here);
    if (!here) {
      return value;
    }
    changed = true;
    Dictionary out = *entries;
    out.set(*key, std::move(replaced));
    return Object::make_dictionary(std::move(out));
  }
  const std::size_t index = std::get<std::size_t>(path[at]);
  const Object::Array* items = resolved.array();
  if (items == nullptr || index >= items->size()) {
    return value;
  }
  bool here = false;
  Object replaced = replace_at(document, (*items)[index], path, at + 1, edits, depth + 1, here);
  if (!here) {
    return value;
  }
  changed = true;
  Object::Array out = *items;
  out[index] = std::move(replaced);
  return Object::make_array(std::move(out));
}

[[nodiscard]] Object direct_of(syntax::Document& document, const Object& value, int depth,
                               std::size_t& items) {
  if (depth > max_depth || ++items > max_items) {
    return {};
  }
  if (value.reference()) {
    const Object resolved = document.resolve(value);
    // A stream cannot be written inside a content stream.
    if (resolved.stream() != nullptr) {
      return {};
    }
    return direct_of(document, resolved, depth + 1, items);
  }
  if (const Object::Array* list = value.array()) {
    Object::Array out;
    out.reserve(list->size());
    for (const Object& item : *list) {
      out.push_back(direct_of(document, item, depth + 1, items));
    }
    return Object::make_array(std::move(out));
  }
  if (const Dictionary* entries = value.dictionary()) {
    Dictionary out;
    for (const auto& [key, item] : entries->entries()) {
      out.set(key, direct_of(document, item, depth + 1, items));
    }
    return Object::make_dictionary(std::move(out));
  }
  return value;
}

}  // namespace

std::vector<PropertyStrings::List> PropertyStrings::lists(syntax::Document& document,
                                                          const text::PageContent& content) {
  std::vector<List> out;
  if (content.page < 1 || content.page > document.pages().size()) {
    return out;
  }
  const syntax::Page& page = document.pages()[content.page - 1];
  // The spans whose /ActualText a glyph reads, by walk and operator.
  std::set<std::pair<std::size_t, std::size_t>> read;
  for (const text::GlyphRecord& record : content.glyphs) {
    if (record.actual_text && *record.actual_text < content.spans.size()) {
      const text::MarkedSpan& span = content.spans[*record.actual_text];
      read.insert({span.stream, span.operation});
    }
  }
  for (std::size_t walk = 0; walk < content.streams.size(); ++walk) {
    const text::ContentStream& stream = content.streams[walk];
    std::optional<std::pair<Object, std::uint32_t>> resources;
    Object properties;
    for (std::size_t index = 0; index < stream.operations.size(); ++index) {
      const syntax::ContentLexer::Operation& operation = stream.operations[index];
      if ((operation.op != "BDC" && operation.op != "DP") || operation.operands.size() < 2 ||
          operation.operands[0].is_name("OC")) {
        continue;
      }
      List list;
      list.walk = walk;
      list.stream = stream.form;
      list.operation = index;
      list.op = operation.op;
      list.tag = operation.operands[0];
      const Object& operand = operation.operands[1];
      if (const Dictionary* inline_list = operand.dictionary()) {
        list.dictionary = *inline_list;
      } else if (const std::string* name = operand.name()) {
        if (!resources) {
          resources = resources_of(document, page, stream);
          properties = properties_of(document, resources->first);
        }
        auto found = named(document, properties, *name, resources->second, content.page);
        if (!found) {
          continue;
        }
        list.name = *name;
        list.identity = std::move(found->first);
        list.dictionary = *found->second.dictionary();
      } else {
        continue;
      }
      list.actual_text_read = read.contains({walk, index});
      list.index = out.size();
      out.push_back(std::move(list));
    }
  }
  return out;
}

std::vector<PropertyStrings::String> PropertyStrings::strings(syntax::Document& document,
                                                              std::span<const List> lists) {
  std::vector<String> out;
  StringWalk walker(document, out);
  for (const List& list : lists) {
    walker.reset();
    for (const auto& [key, value] : list.dictionary.entries()) {
      const Surface surface = surface_of(key);
      if (surface == Surface::actual_text && list.actual_text_read) {
        continue;
      }
      Path path{key};
      walker.walk(value, path, list.index, surface, 1);
    }
  }
  return out;
}

std::string_view PropertyStrings::surface_name(Surface surface) noexcept {
  switch (surface) {
    case Surface::alt:
      return "alt";
    case Surface::expansion:
      return "expansion";
    case Surface::actual_text:
      return "actual_text";
    case Surface::property:
      break;
  }
  return "property";
}

std::map<PropertyStrings::Identity, std::size_t> PropertyStrings::uses(
    syntax::Document& document, const std::set<std::uint32_t>& pages) {
  UseCounter::Counts out;
  UseCounter counter(document);
  for (const std::uint32_t number : pages) {
    if (number < 1 || number > document.pages().size()) {
      continue;
    }
    const syntax::Page& page = document.pages()[number - 1];
    const std::vector<std::vector<std::byte>> pieces =
        document.content_pieces(page.contents, number);
    std::vector<std::uint32_t> path;
    counter.count(pieces, page.resources, 0, number, 0, path, out);
  }
  return out;
}

std::u32string PropertyStrings::edited(std::u32string_view text, std::vector<Edit> edits) {
  std::ranges::stable_sort(edits, [](const Edit& a, const Edit& b) {
    if (a.first != b.first) {
      return a.first < b.first;
    }
    if (a.last != b.last) {
      return a.last > b.last;
    }
    return a.order < b.order;
  });
  std::vector<Edit> merged;
  for (Edit& next : edits) {
    next.last = std::min(next.last, text.size());
    if (next.first >= next.last) {
      continue;
    }
    if (!merged.empty()) {
      Edit& current = merged.back();
      if (next.first <= current.last && next.placeholder == current.placeholder) {
        current.last = std::max(current.last, next.last);
        continue;
      }
      if (next.first < current.last) {
        next.first = current.last;
        if (next.first >= next.last) {
          continue;
        }
      }
    }
    merged.push_back(std::move(next));
  }
  std::u32string out;
  std::size_t cursor = 0;
  for (const Edit& edit : merged) {
    out.append(text.substr(cursor, edit.first - cursor));
    out += TextFolding::decode(edit.placeholder);
    cursor = edit.last;
  }
  out.append(text.substr(cursor));
  return out;
}

std::optional<syntax::Dictionary> PropertyStrings::apply(syntax::Document& document,
                                                         const syntax::Dictionary& dictionary,
                                                         std::span<const Edit> edits) {
  std::map<std::string, std::vector<Edit>> by_path;
  for (const Edit& edit : edits) {
    by_path[path_key(edit.path)].push_back(edit);
  }
  Object value = Object::make_dictionary(dictionary);
  bool changed = false;
  for (const auto& [key, group] : by_path) {
    value = replace_at(document, value, group.front().path, 0, group, 0, changed);
  }
  if (!changed || value.dictionary() == nullptr) {
    return std::nullopt;
  }
  return *value.dictionary();
}

syntax::Object PropertyStrings::direct(syntax::Document& document, const syntax::Object& value) {
  std::size_t items = 0;
  return direct_of(document, value, 0, items);
}

std::pair<std::string, std::string> PropertyStrings::glued(std::u32string_view text,
                                                           std::size_t first, std::size_t last) {
  first = std::min(first, text.size());
  last = std::min(std::max(last, first), text.size());
  std::size_t start = first;
  while (start > 0 && !TextFolding::is_white_space(text[start - 1])) {
    --start;
  }
  std::size_t end = last;
  while (end < text.size() && !TextFolding::is_white_space(text[end])) {
    ++end;
  }
  return {TextFolding::encode(text.substr(start, first - start)),
          TextFolding::encode(text.substr(last, end - last))};
}

std::string PropertyStrings::path_key(const Path& path) {
  std::string out;
  for (const Step& step : path) {
    if (const std::string* key = std::get_if<std::string>(&step)) {
      out += 'k' + std::to_string(key->size()) + ':' + *key;
    } else {
      out += 'i' + std::to_string(std::get<std::size_t>(step)) + ';';
    }
  }
  return out;
}

}  // namespace pdfkit::redact
