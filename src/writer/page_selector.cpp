// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The flat page tree and the resource pruning. A stream with resources of its
// own (a form, a tiling pattern, a soft-mask group) is pruned once, by its own
// content, whatever draws it; one without them adds its names to the
// dictionary it reads from. Pruning nests at most 64 streams deep, so a
// hostile chain of forms cannot exhaust the stack; past that depth a
// dictionary is kept whole and counted.

#include "writer/page_selector.hpp"

#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"
#include "syntax/page.hpp"
#include "syntax/stream.hpp"
#include "writer/resource_usage.hpp"

#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace pdfkit::writer {

namespace {

using syntax::Dictionary;
using syntax::Object;

constexpr int max_depth = 64;

Object number_object(double value) {
  if (std::isfinite(value) && std::abs(value) < 1e15 && value == std::floor(value)) {
    return Object::make_integer(static_cast<std::int64_t>(value));
  }
  return Object::make_real(value);
}

Object box_object(const syntax::Page::Box& box) {
  return Object::make_array({number_object(box.left), number_object(box.bottom),
                             number_object(box.right), number_object(box.top)});
}

class Pruner {
 public:
  explicit Pruner(ObjectGraph& graph) : graph_(graph) {}

  [[nodiscard]] std::size_t unpruned() const noexcept { return unpruned_; }

  // The names a decoded stream uses; incomplete when it did not decode whole.
  ResourceUsage usage_of(const syntax::Stream& stream, const std::string& what) {
    const syntax::Filters::Result decoded = graph_.document().decode(stream);
    ResourceUsage usage = ResourceUsage::of({decoded.bytes}, graph_.document().limits(), what);
    usage.complete = usage.complete && !decoded.damaged && decoded.remaining.empty();
    return usage;
  }

  // The names a page's /Contents uses.
  ResourceUsage usage_of_contents(const Object& contents, std::uint32_t page) {
    std::vector<std::vector<std::byte>> pieces;
    bool whole = true;
    const auto take = [&](const Object& piece) {
      const Object value = graph_.resolve(piece);
      if (const syntax::Stream* stream = value.stream()) {
        syntax::Filters::Result decoded = graph_.document().decode(*stream, page);
        whole = whole && !decoded.damaged && decoded.remaining.empty();
        pieces.push_back(std::move(decoded.bytes));
      }
    };
    const Object value = graph_.resolve(contents);
    if (const Object::Array* list = value.array()) {
      for (const Object& piece : *list) {
        take(piece);
      }
    } else {
      take(value);
    }
    ResourceUsage usage =
        ResourceUsage::of(pieces, graph_.document().limits(), "page " + std::to_string(page));
    usage.complete = usage.complete && whole;
    return usage;
  }

  // resources pruned to usage, as a direct dictionary; resources itself when
  // it is no dictionary or usage is incomplete. changed says which.
  Object prune(const Object& resources, ResourceUsage usage, bool& changed) {
    changed = false;
    const Object resolved = graph_.resolve(resources);
    const Dictionary* dictionary = resolved.dictionary();
    if (dictionary == nullptr) {
      return resources;
    }
    close_over(*dictionary, usage);
    if (!usage.complete) {
      ++unpruned_;
      return resources;
    }
    Dictionary out;
    for (const auto& [key, value] : dictionary->entries()) {
      if (key == "ProcSet") {
        out.set(key, value);
        continue;
      }
      bool known = false;
      for (const std::string_view category : ResourceUsage::categories) {
        known = known || category == key;
      }
      if (!known) {
        continue;
      }
      const Object group = graph_.resolve(value);
      const Dictionary* entries = group.dictionary();
      if (entries == nullptr) {
        continue;
      }
      Dictionary kept;
      for (const auto& [name, entry] : entries->entries()) {
        // /DefaultGray, /DefaultRGB and /DefaultCMYK are named by no operator:
        // they replace the device spaces wherever those are used (ISO 32000-1
        // section 8.6.5.6, read).
        const bool default_space = key == "ColorSpace" && (name == "DefaultGray" ||
                                                           name == "DefaultRGB" ||
                                                           name == "DefaultCMYK");
        if (default_space || usage.uses(key, name)) {
          kept.set(name, entry);
        }
      }
      if (!kept.empty()) {
        out.set(key, Object::make_dictionary(std::move(kept)));
      }
    }
    changed = true;
    return Object::make_dictionary(std::move(out));
  }

 private:
  // A stream with resources of its own, pruned by its own content once.
  void prune_own(std::uint32_t number) {
    if (!done_.insert(number).second) {
      return;
    }
    const Object value = graph_.get(number);
    const syntax::Stream* stream = value.stream();
    if (stream == nullptr) {
      return;
    }
    if (depth_ >= max_depth) {
      ++unpruned_;
      return;
    }
    const Object& resources = stream->dictionary().get("Resources");
    ResourceUsage usage = usage_of(*stream, "object " + std::to_string(number));
    ++depth_;
    bool changed = false;
    Object pruned = prune(resources, std::move(usage), changed);
    --depth_;
    if (changed) {
      Dictionary dictionary = stream->dictionary();
      dictionary.set("Resources", std::move(pruned));
      graph_.set(number, Object::make_stream(stream->with_dictionary(std::move(dictionary))));
    }
  }

  // A stream drawn from resources: pruned on its own, or its names added to
  // usage when it reads its user's resources.
  void drawn(const Object& entry, ResourceUsage& usage, std::set<std::uint32_t>& merged,
             bool& grew) {
    const auto reference = entry.reference();
    const Object value = graph_.resolve(entry);
    const syntax::Stream* stream = value.stream();
    if (stream == nullptr || !reference) {
      return;
    }
    if (graph_.resolve_entry(stream->dictionary(), "Resources").dictionary() != nullptr) {
      prune_own(reference->number);
      return;
    }
    if (merged.insert(reference->number).second) {
      usage.merge(usage_of(*stream, "object " + std::to_string(reference->number)));
      grew = true;
    }
  }

  Object entry_of(const Dictionary& resources, std::string_view category,
                  const std::string& name) {
    const Object group = graph_.resolve_entry(resources, category);
    if (const Dictionary* entries = group.dictionary()) {
      return entries->get(name);
    }
    return {};
  }

  // Follows the names of usage to the streams they draw until no new name
  // appears.
  void close_over(const Dictionary& resources, ResourceUsage& usage) {
    std::set<std::pair<std::string, std::string>> examined;
    std::set<std::uint32_t> merged;
    bool grew = true;
    while (grew) {
      grew = false;
      for (const std::string_view category : {"XObject", "Pattern", "ExtGState", "Font"}) {
        const auto found = usage.names.find(category);
        if (found == usage.names.end()) {
          continue;
        }
        const std::set<std::string> names = found->second;
        for (const std::string& name : names) {
          if (!examined.emplace(std::string(category), name).second) {
            continue;
          }
          const Object entry = entry_of(resources, category, name);
          const Object value = graph_.resolve(entry);
          if (category == "XObject") {
            if (const syntax::Stream* stream = value.stream();
                stream != nullptr && stream->dictionary().get("Subtype").is_name("Form")) {
              drawn(entry, usage, merged, grew);
            }
          } else if (category == "Pattern") {
            if (const syntax::Stream* stream = value.stream(); stream != nullptr) {
              drawn(entry, usage, merged, grew);
            }
          } else if (category == "ExtGState") {
            const Dictionary* state = value.dictionary();
            if (state == nullptr) {
              continue;
            }
            const Object mask = graph_.resolve_entry(*state, "SMask");
            if (const Dictionary* soft = mask.dictionary()) {
              drawn(soft->get("G"), usage, merged, grew);
            }
          } else {
            const Dictionary* font = value.dictionary();
            if (font == nullptr || !font->get("Subtype").is_name("Type3") ||
                graph_.resolve_entry(*font, "Resources").dictionary() != nullptr) {
              continue;
            }
            const Object procedures = graph_.resolve_entry(*font, "CharProcs");
            if (const Dictionary* glyphs = procedures.dictionary()) {
              for (const auto& glyph : glyphs->entries()) {
                drawn(glyph.second, usage, merged, grew);
              }
            }
          }
        }
      }
    }
  }

  ObjectGraph& graph_;
  std::set<std::uint32_t> done_;
  std::size_t unpruned_ = 0;
  int depth_ = 0;
};

}  // namespace

PageSelector::Result PageSelector::apply(ObjectGraph& graph,
                                         const std::vector<std::uint32_t>& pages) {
  const std::vector<syntax::Page>& source = graph.document().pages();
  Result result;
  if (pages.empty()) {
    for (const syntax::Page& page : source) {
      result.pages.push_back(page.number);
    }
  } else {
    result.pages = pages;
  }
  Pruner pruner(graph);
  const Object::Reference node = graph.add(Object());
  Object::Array kids;
  std::set<std::uint32_t> placed;
  for (const std::uint32_t number : result.pages) {
    const syntax::Page& page = source.at(number - 1);
    Dictionary dictionary = page.dictionary;
    dictionary.set("Parent", Object::make_reference(node));
    if (graph.resolve_entry(dictionary, "Resources").dictionary() == nullptr &&
        page.resources.dictionary() != nullptr) {
      dictionary.set("Resources", page.resources);
    }
    bool changed = false;
    Object pruned = pruner.prune(dictionary.get("Resources"),
                                 pruner.usage_of_contents(page.contents, page.number), changed);
    if (changed) {
      dictionary.set("Resources", std::move(pruned));
    }
    if (!dictionary.contains("MediaBox")) {
      dictionary.set("MediaBox", box_object(page.media_box));
    }
    if (!dictionary.contains("CropBox") && page.crop_box != page.media_box) {
      dictionary.set("CropBox", box_object(page.crop_box));
    }
    if (!dictionary.contains("Rotate") && page.rotate != 0) {
      dictionary.set("Rotate", Object::make_integer(page.rotate));
    }
    const std::uint32_t own = page.reference.number;
    if (own != 0 && placed.insert(own).second) {
      graph.set(own, Object::make_dictionary(std::move(dictionary)));
      kids.push_back(Object::make_reference({own, 0}));
    } else {
      kids.push_back(Object::make_reference(graph.add(Object::make_dictionary(std::move(dictionary)))));
    }
  }
  Dictionary tree;
  tree.set("Type", Object::make_name("Pages"));
  tree.set("Kids", Object::make_array(std::move(kids)));
  tree.set("Count", Object::make_integer(static_cast<std::int64_t>(result.pages.size())));
  graph.set(node.number, Object::make_dictionary(std::move(tree)));

  const std::uint32_t root = graph.root();
  const Object catalog = root != 0 ? graph.get(root) : Object();
  if (const Dictionary* dictionary = catalog.dictionary()) {
    Dictionary updated = *dictionary;
    updated.set("Pages", Object::make_reference(node));
    graph.set(root, Object::make_dictionary(std::move(updated)));
  } else {
    Dictionary created;
    created.set("Type", Object::make_name("Catalog"));
    created.set("Pages", Object::make_reference(node));
    Dictionary trailer = graph.trailer();
    trailer.set("Root", Object::make_reference(graph.add(Object::make_dictionary(std::move(created)))));
    graph.set_trailer(std::move(trailer));
  }
  result.unpruned = pruner.unpruned();
  return result;
}

}  // namespace pdfkit::writer
