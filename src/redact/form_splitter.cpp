// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The draws are counted from the operators alone: each Do of a name the
// current resources map to a form XObject (ISO 32000-1 section 8.10, read)
// is one draw, and the form's own Do operators count again for every draw
// of it, through its /Resources or, without them, the resources it was
// drawn under. A form that draws itself, or forms nested deeper than the
// interpreter walks, stop the count where the interpreter stops. The count
// of a form with resources of its own is computed once.

#include "redact/form_splitter.hpp"

#include "syntax/content_lexer.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace pdfkit::redact {

namespace {

// As deep as the interpreter walks forms.
constexpr int max_depth = 32;

class Counter {
 public:
  explicit Counter(syntax::Document& document) : document_(document) {}

  // The forms one draw of pieces under resources draws, with how often.
  std::map<std::uint32_t, std::size_t> of(const std::vector<std::vector<std::byte>>& pieces,
                                          const syntax::Object& resources, int depth,
                                          std::vector<std::uint32_t>& path) {
    std::map<std::uint32_t, std::size_t> out;
    if (depth > max_depth) {
      return out;
    }
    syntax::ContentLexer lexer(pieces, document_.limits(), "a form count");
    const syntax::Object resolved = document_.resolve(resources);
    const syntax::Dictionary* entries = resolved.dictionary();
    const syntax::Object group =
        entries != nullptr ? document_.resolve_entry(*entries, "XObject") : syntax::Object();
    const syntax::Dictionary* names = group.dictionary();
    while (const std::optional<syntax::ContentLexer::Operation> operation = lexer.next()) {
      if (operation->op != "Do" || operation->operands.empty() || names == nullptr) {
        continue;
      }
      const std::string* name = operation->operands.front().name();
      const syntax::Object* raw = name != nullptr ? names->find(*name) : nullptr;
      const std::optional<syntax::Object::Reference> reference =
          raw != nullptr ? raw->reference() : std::nullopt;
      if (!reference) {
        continue;
      }
      const syntax::Object value = document_.resolve(*raw);
      const syntax::Stream* stream = value.stream();
      if (stream == nullptr ||
          !document_.resolve_entry(stream->dictionary(), "Subtype").is_name("Form") ||
          std::ranges::find(path, reference->number) != path.end()) {
        continue;
      }
      ++out[reference->number];
      for (const auto& [form, count] : inner(reference->number, *stream, resources, depth, path)) {
        out[form] += count;
      }
    }
    return out;
  }

 private:
  // What one draw of a form draws.
  std::map<std::uint32_t, std::size_t> inner(std::uint32_t number, const syntax::Stream& stream,
                                             const syntax::Object& outer, int depth,
                                             std::vector<std::uint32_t>& path) {
    const syntax::Object own = document_.resolve_entry(stream.dictionary(), "Resources");
    const bool has_own = own.dictionary() != nullptr;
    if (has_own) {
      if (const auto found = memo_.find(number); found != memo_.end()) {
        return found->second;
      }
    }
    const std::vector<std::vector<std::byte>> pieces{document_.decode(stream).bytes};
    path.push_back(number);
    std::map<std::uint32_t, std::size_t> out = of(pieces, has_own ? own : outer, depth + 1, path);
    path.pop_back();
    if (has_own) {
      memo_.emplace(number, out);
    }
    return out;
  }

  syntax::Document& document_;
  std::map<std::uint32_t, std::map<std::uint32_t, std::size_t>> memo_;
};

// The walk whose resources resolve a name the stream of walk reads: the
// nearest walk on its path with resources of its own, else the page (0).
std::size_t holder_of(const TextPlan::PageWork& work, std::size_t walk) {
  while (walk != 0) {
    const auto found = work.walks.find(walk);
    if (found == work.walks.end()) {
      return 0;
    }
    if (found->second.resources) {
      return walk;
    }
    walk = found->second.parent;
  }
  return 0;
}

}  // namespace

std::map<std::uint32_t, std::size_t> FormSplitter::draws(syntax::Document& document,
                                                         const std::set<std::uint32_t>& pages) {
  std::map<std::uint32_t, std::size_t> out;
  Counter counter(document);
  for (const std::uint32_t number : pages) {
    if (number < 1 || number > document.pages().size()) {
      continue;
    }
    const syntax::Page& page = document.pages()[number - 1];
    const std::vector<std::vector<std::byte>> pieces =
        document.content_pieces(page.contents, number);
    std::vector<std::uint32_t> path;
    for (const auto& [form, count] : counter.of(pieces, page.resources, 0, path)) {
      out[form] += count;
    }
  }
  return out;
}

FormSplitter::Plan FormSplitter::plan(std::uint32_t number, const TextPlan::PageWork& work,
                                      const std::map<std::uint32_t, std::size_t>& draws) {
  Plan out;
  for (const auto& [walk, target] : work.splits) {
    if (!target.empty()) {
      out.local[walk].merge(target);
    }
  }
  // Children have higher walk indices than the walks that draw them, so one
  // pass downwards sees every child's rename before its parent is decided.
  for (const auto& [walk, info] : std::views::reverse(work.walks)) {
    const auto changed = out.local.find(walk);
    if (changed == out.local.end() || changed->second.empty()) {
      continue;
    }
    const auto drawn = draws.find(info.form);
    if (drawn == draws.end() || drawn->second < 2) {
      continue;
    }
    Copy copy;
    copy.walk = walk;
    copy.form = info.form;
    copy.name = "PdfkitOcc" + std::to_string(number) + "W" + std::to_string(walk);
    copy.parent = info.parent;
    out.local[info.parent].replaced[info.drawn_at] = "/" + copy.name + " Do";
    out.names[holder_of(work, info.parent)][copy.name] = walk;
    out.copied.insert(walk);
    out.copies.push_back(std::move(copy));
  }
  return out;
}

}  // namespace pdfkit::redact
