// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The walk. Every content stream is read from the object graph, so what the
// text surgery wrote is what is edited here. A Do's name is looked up in the
// resources of the stream that runs it; a form's operations are looked up in
// its own resources, or in its user's when it has none (ISO 32000-1 section
// 7.8.3, recalled), which is why such a form is walked once per page and its
// edits merged. A tiling pattern or a soft-mask group the resources name is
// walked with its own resources too: the interpreter does not number the
// images drawn there, but an image removed from the pages must not survive
// in a pattern either. Forms nest at most 32 deep, as the interpreter's do.

#include "redact/image_remover.hpp"

#include "images/image_identity.hpp"
#include "syntax/content_lexer.hpp"
#include "text/content_serialiser.hpp"
#include "text/content_stream.hpp"
#include "writer/stream_copier.hpp"

#include <limits>
#include <span>
#include <utility>

namespace pdfkit::redact {

namespace {

using syntax::Dictionary;
using syntax::Object;

constexpr int max_depth = 32;
// The page index of a form walked with its own resources: one walk serves
// every page.
constexpr std::size_t own_resources = std::numeric_limits<std::size_t>::max();

text::ContentStream lex(const std::vector<std::vector<std::byte>>& pieces,
                        syntax::Document& document, const std::string& what) {
  syntax::ContentLexer lexer(pieces, document.limits(), what);
  text::ContentStream stream;
  while (std::optional<syntax::ContentLexer::Operation> operation = lexer.next()) {
    stream.operations.push_back(std::move(*operation));
  }
  const platform::ByteSpan joined = lexer.bytes();
  stream.bytes = std::make_shared<const std::vector<std::byte>>(joined.begin(), joined.end());
  return stream;
}

class Walker {
 public:
  Walker(writer::ObjectGraph& graph, const ImageRemover::Request& request)
      : graph_(graph), document_(graph.document()), request_(request) {}

  void page(std::uint32_t object, std::size_t index) {
    const Object value = graph_.get(object);
    const Dictionary* found = value.dictionary();
    if (found == nullptr) {
      return;
    }
    Dictionary dictionary = *found;
    const Object resources = graph_.resolve(dictionary.get("Resources"));
    std::vector<std::vector<std::byte>> pieces;
    const Object contents = graph_.resolve(dictionary.get("Contents"));
    const auto add_piece = [&](const Object& item) {
      const Object resolved = graph_.resolve(item);
      if (const syntax::Stream* stream = resolved.stream()) {
        pieces.push_back(document_.decode(*stream).bytes);
      }
    };
    if (const Object::Array* list = contents.array()) {
      for (const Object& item : *list) {
        add_piece(item);
      }
    } else {
      add_piece(contents);
    }
    bool changed = false;
    if (!pieces.empty()) {
      const text::ContentStream stream =
          lex(pieces, document_, "page " + std::to_string(index + 1));
      const std::map<std::size_t, std::string> edits = walk(stream, resources, index, 0);
      if (!edits.empty()) {
        const text::ContentSerialiser::Result written =
            text::ContentSerialiser::write(stream, 0, {}, edits);
        dictionary.set("Contents",
                       Object::make_reference(graph_.add(Object::make_stream(
                           writer::StreamCopier::compress(Dictionary(), written.bytes)))));
        ++result_.streams_rewritten;
        changed = true;
      }
    }
    if (const std::optional<Object> stripped = strip(dictionary.get("Resources"))) {
      dictionary.set("Resources", *stripped);
      changed = true;
    }
    if (changed) {
      graph_.set(object, Object::make_dictionary(std::move(dictionary)));
    }
  }

  // Writes every form, pattern and group whose operations or resources changed.
  void finish() {
    for (const auto& [number, edits] : form_edits_) {
      const Object value = graph_.get(number);
      const syntax::Stream* stream = value.stream();
      if (stream == nullptr) {
        continue;
      }
      Dictionary dictionary = stream->dictionary();
      bool changed = false;
      if (const std::optional<Object> stripped = strip(dictionary.get("Resources"))) {
        dictionary.set("Resources", *stripped);
        changed = true;
      }
      if (!edits.empty()) {
        const text::ContentSerialiser::Result written =
            text::ContentSerialiser::write(form_walks_.at(number), 0, {}, edits);
        for (const std::string_view key : {"Filter", "DecodeParms", "Length", "DL"}) {
          dictionary.erase(key);
        }
        graph_.set(number, Object::make_stream(writer::StreamCopier::compress(std::move(dictionary),
                                                                              written.bytes)));
        ++result_.streams_rewritten;
      } else if (changed) {
        graph_.set(number, Object::make_stream(stream->with_dictionary(std::move(dictionary))));
      }
    }
  }

  [[nodiscard]] const ImageRemover::Result& result() const noexcept { return result_; }

 private:
  // The edits of one stream's operations, read with resources; enters every
  // form it draws and every pattern and group its resources name.
  std::map<std::size_t, std::string> walk(const text::ContentStream& stream,
                                          const Object& resources, std::size_t page, int depth) {
    std::map<std::size_t, std::string> edits;
    const Dictionary* entries = resources.dictionary();
    const Object xobjects = entries != nullptr ? graph_.resolve(entries->get("XObject")) : Object();
    const Object colour_spaces =
        entries != nullptr ? graph_.resolve(entries->get("ColorSpace")) : Object();
    if (entries != nullptr) {
      extras(*entries, resources, page, depth);
    }
    const std::vector<std::byte>& bytes = *stream.bytes;
    for (std::size_t i = 0; i < stream.operations.size(); ++i) {
      const syntax::ContentLexer::Operation& operation = stream.operations[i];
      if (operation.op == "Do" && !operation.operands.empty()) {
        const std::string* name = operation.operands.back().name();
        const Dictionary* names = xobjects.dictionary();
        if (name == nullptr || names == nullptr) {
          continue;
        }
        const Object& entry = names->get(*name);
        const std::optional<Object::Reference> reference = entry.reference();
        if (reference) {
          if (const auto removed = request_.objects.find(reference->number);
              removed != request_.objects.end()) {
            edits[i] = std::string();
            ++result_.removed[removed->second];
            continue;
          }
        }
        if (reference) {
          const Object target = graph_.get(reference->number);
          const syntax::Stream* drawn = target.stream();
          const Object* subtype = drawn != nullptr ? drawn->dictionary().find("Subtype") : nullptr;
          if (subtype != nullptr && subtype->name() != nullptr && *subtype->name() == "Form") {
            form(reference->number, resources, page, depth + 1);
          }
        }
      } else if (operation.op == "BI" && !operation.operands.empty()) {
        const Dictionary* dictionary = operation.operands[0].dictionary();
        if (dictionary == nullptr || operation.data_end > bytes.size() ||
            operation.data_start > operation.data_end) {
          continue;
        }
        const platform::ByteSpan data(bytes.data() + operation.data_start,
                                      operation.data_end - operation.data_start);
        const std::string identity = images::ImageIdentity::of_inline(document_, *dictionary, data);
        if (request_.inline_identities.contains(identity)) {
          edits[i] = std::string();
          ++result_.removed[identity];
        } else if (request_.inline_treated.contains(identity) && request_.edit) {
          if (std::optional<std::string> replaced =
                  request_.edit(identity, *dictionary, data, colour_spaces)) {
            edits[i] = std::move(*replaced);
            ++result_.replaced[identity];
          }
        }
      }
    }
    return edits;
  }

  // The tiling patterns and soft-mask groups resources names.
  void extras(const Dictionary& entries, const Object& resources, std::size_t page, int depth) {
    if (const Dictionary* patterns = graph_.resolve(entries.get("Pattern")).dictionary()) {
      for (const auto& [name, value] : patterns->entries()) {
        if (const auto reference = value.reference()) {
          const Object pattern = graph_.get(reference->number);
          if (pattern.stream() != nullptr) {
            form(reference->number, resources, page, depth + 1);
          }
        }
      }
    }
    if (const Dictionary* states = graph_.resolve(entries.get("ExtGState")).dictionary()) {
      for (const auto& [name, value] : states->entries()) {
        const Object state = graph_.resolve(value);
        const Dictionary* fields = state.dictionary();
        const Object mask = fields != nullptr ? graph_.resolve(fields->get("SMask")) : Object();
        const Dictionary* soft = mask.dictionary();
        if (soft == nullptr) {
          continue;
        }
        if (const auto group = soft->get("G").reference()) {
          form(group->number, resources, page, depth + 1);
        }
      }
    }
  }

  // Walks the form (or pattern, or group) number with its own resources or,
  // having none, with inherited.
  void form(std::uint32_t number, const Object& inherited, std::size_t page, int depth) {
    if (depth > max_depth) {
      return;
    }
    const Object value = graph_.get(number);
    const syntax::Stream* stream = value.stream();
    if (stream == nullptr) {
      return;
    }
    const Object* own = stream->dictionary().find("Resources");
    const std::size_t context = own != nullptr ? own_resources : page;
    if (!entered_.insert({number, context}).second) {
      return;
    }
    auto walked = form_walks_.find(number);
    if (walked == form_walks_.end()) {
      const std::vector<std::vector<std::byte>> pieces{document_.decode(*stream).bytes};
      walked = form_walks_.emplace(number, lex(pieces, document_, "form " + std::to_string(number)))
                   .first;
    }
    form_edits_[number];
    const Object resources = own != nullptr ? graph_.resolve(*own) : inherited;
    for (auto& [index, bytes] : walk(walked->second, resources, page, depth)) {
      form_edits_[number][index] = std::move(bytes);
    }
  }

  // resources without the entries that name a removed XObject: a reference
  // to the edited dictionary (replaced under its number) or the edited
  // dictionary itself; no value when nothing names one.
  std::optional<Object> strip(const Object& value) {
    if (request_.objects.empty()) {
      return std::nullopt;
    }
    const Object resources = graph_.resolve(value);
    const Dictionary* entries = resources.dictionary();
    if (entries == nullptr) {
      return std::nullopt;
    }
    const Object& xobject_value = entries->get("XObject");
    const Object xobjects = graph_.resolve(xobject_value);
    const Dictionary* names = xobjects.dictionary();
    if (names == nullptr) {
      return std::nullopt;
    }
    Dictionary kept;
    bool dropped = false;
    for (const auto& [name, entry] : names->entries()) {
      const auto reference = entry.reference();
      if (reference && request_.objects.contains(reference->number)) {
        dropped = true;
      } else {
        kept.set(name, entry);
      }
    }
    if (!dropped) {
      return std::nullopt;
    }
    Dictionary edited = *entries;
    if (const auto reference = xobject_value.reference()) {
      graph_.set(reference->number, Object::make_dictionary(std::move(kept)));
    } else {
      edited.set("XObject", Object::make_dictionary(std::move(kept)));
    }
    if (const auto reference = value.reference()) {
      graph_.set(reference->number, Object::make_dictionary(std::move(edited)));
      return std::nullopt;
    }
    return Object::make_dictionary(std::move(edited));
  }

  writer::ObjectGraph& graph_;
  syntax::Document& document_;
  const ImageRemover::Request& request_;
  ImageRemover::Result result_;
  std::map<std::uint32_t, text::ContentStream> form_walks_;
  std::map<std::uint32_t, std::map<std::size_t, std::string>> form_edits_;
  std::set<std::pair<std::uint32_t, std::size_t>> entered_;
};

}  // namespace

ImageRemover::Result ImageRemover::apply(writer::ObjectGraph& graph,
                                         const std::vector<std::uint32_t>& page_objects,
                                         const Request& request) {
  Walker walker(graph, request);
  for (std::size_t i = 0; i < page_objects.size(); ++i) {
    if (!request.pages || request.pages->contains(i)) {
      walker.page(page_objects[i], i);
    }
  }
  walker.finish();
  return walker.result();
}

}  // namespace pdfkit::redact
