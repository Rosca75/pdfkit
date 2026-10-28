// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The walk. Whether a group is off is text::OptionalContent's answer, the one
// the extraction gives as the optional_content_off class, so what the write
// removes is what a caller saw marked hidden. Marked content nests: inside an
// off sequence every opener and EMC is counted and removed, and the sequence
// ends at the EMC that closes its own opener; a stream that never closes it
// is off to its end. The text state (font, size, Tc, Tw, Tz) is followed
// through q and Q so a removed text-showing operator's advance is the one the
// interpreter gives (docs/DESIGN.md section 6.1: tx = ((w0 - adj/1000) * Tfs
// + Tc + Tw) * Th, Tw on a one-byte code 32 alone, ty by w1 for vertical
// writing); a form starts from the default text state, and an operator
// whose font is unknown or whose size is zero is removed without its
// advance. Every resource dictionary is stripped after every walk, because a
// dictionary shared by two pages must still name the groups when the second
// page is walked. Forms nest at most 32 deep, as the interpreter's do.

#include "redact/optional_content_remover.hpp"

#include "fonts/font_loader.hpp"
#include "platform/byte_span.hpp"
#include "redact/content_syntax.hpp"
#include "syntax/content_lexer.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "text/content_serialiser.hpp"
#include "text/content_stream.hpp"
#include "text/optional_content.hpp"
#include "writer/stream_copier.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::redact {

namespace {

using syntax::Dictionary;
using syntax::Object;

constexpr int max_depth = 32;
// The page index of a form walked with its own resources: one walk serves
// every page.
constexpr std::size_t own_resources = std::numeric_limits<std::size_t>::max();

// ISO 32000-1 Annex A, table A.1 (read): the path construction and painting
// operators, sh with them because it paints.
constexpr std::array<std::string_view, 7> construction{"m", "l", "c", "v", "y", "h", "re"};
constexpr std::array<std::string_view, 11> painting{"S",  "s", "f",  "F", "f*", "B",
                                                    "B*", "b", "b*", "n", "sh"};

template <std::size_t N>
bool listed(const std::array<std::string_view, N>& list, std::string_view op) {
  for (const std::string_view item : list) {
    if (item == op) {
      return true;
    }
  }
  return false;
}

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

// The text state a removed text-showing operator's advance is computed from.
struct TextState {
  Object font;
  double size = 0.0;
  double char_spacing = 0.0;
  double word_spacing = 0.0;
  double scaling = 100.0;
};

// One open marked-content sequence.
struct Mark {
  // Its tag is /OC, so its opener and EMC are removed.
  bool optional = false;
  // It opened the off stretch: its EMC ends it.
  bool opens_off = false;
};

double number_of(const Object& value) {
  return value.number().value_or(0.0);
}

class Walker {
 public:
  Walker(writer::ObjectGraph& graph, const text::OptionalContent& optional)
      : graph_(graph), document_(graph.document()), optional_(optional), loader_(document_) {}

  void page(std::uint32_t object, std::size_t index) {
    const Object value = graph_.get(object);
    const Dictionary* found = value.dictionary();
    if (found == nullptr) {
      return;
    }
    pages_.push_back(object);
    const Object resources = graph_.resolve(found->get("Resources"));
    std::vector<std::vector<std::byte>> pieces;
    const Object contents = graph_.resolve(found->get("Contents"));
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
    if (pieces.empty()) {
      return;
    }
    const text::ContentStream stream = lex(pieces, document_, "page " + std::to_string(index + 1));
    const std::map<std::size_t, std::string> edits = walk(stream, resources, index, 0);
    if (edits.empty()) {
      return;
    }
    const text::ContentSerialiser::Result written =
        text::ContentSerialiser::write(stream, 0, {}, edits);
    Dictionary dictionary = *found;
    dictionary.set("Contents", Object::make_reference(graph_.add(Object::make_stream(
                                   writer::StreamCopier::compress(Dictionary(), written.bytes)))));
    graph_.set(object, Object::make_dictionary(std::move(dictionary)));
    ++result_.streams_rewritten;
  }

  // Writes every form whose operations changed, then strips every resource
  // dictionary walked.
  void finish() {
    for (const auto& [number, edits] : form_edits_) {
      if (edits.empty()) {
        continue;
      }
      const Object value = graph_.get(number);
      const syntax::Stream* stream = value.stream();
      if (stream == nullptr) {
        continue;
      }
      Dictionary dictionary = stream->dictionary();
      const text::ContentSerialiser::Result written =
          text::ContentSerialiser::write(form_walks_.at(number), 0, {}, edits);
      for (const std::string_view key : {"Filter", "DecodeParms", "Length", "DL"}) {
        dictionary.erase(key);
      }
      graph_.set(number, Object::make_stream(
                             writer::StreamCopier::compress(std::move(dictionary), written.bytes)));
      ++result_.streams_rewritten;
    }
    for (const std::uint32_t object : pages_) {
      const Object value = graph_.get(object);
      if (const Dictionary* found = value.dictionary()) {
        if (const std::optional<Object> stripped = strip(found->get("Resources"))) {
          Dictionary dictionary = *found;
          dictionary.set("Resources", *stripped);
          graph_.set(object, Object::make_dictionary(std::move(dictionary)));
        }
      }
    }
    for (const auto& [number, edits] : form_edits_) {
      const Object value = graph_.get(number);
      const syntax::Stream* stream = value.stream();
      if (stream == nullptr) {
        continue;
      }
      if (const std::optional<Object> stripped = strip(stream->dictionary().get("Resources"))) {
        Dictionary dictionary = stream->dictionary();
        dictionary.set("Resources", *stripped);
        graph_.set(number, Object::make_stream(stream->with_dictionary(std::move(dictionary))));
      }
    }
  }

  [[nodiscard]] const OptionalContentRemover::Result& result() const noexcept { return result_; }

 private:
  // Whether the XObject value (a reference or a stream) has an /OC the
  // default configuration turns off.
  bool xobject_off(const Object& value) {
    const Object resolved = graph_.resolve(value);
    const syntax::Stream* stream = resolved.stream();
    const Object* membership = stream != nullptr ? stream->dictionary().find("OC") : nullptr;
    return membership != nullptr && optional_.hidden(*membership);
  }

  // Whether a /Properties entry is a group or a membership dictionary.
  bool names_group(const Object& value) {
    const Object resolved = graph_.resolve(value);
    const Dictionary* entries = resolved.dictionary();
    if (entries == nullptr) {
      return false;
    }
    const Object type = graph_.resolve_entry(*entries, "Type");
    return type.is_name("OCG") || type.is_name("OCMD");
  }

  // The group or membership dictionary a BDC's properties operand names: an
  // inline dictionary, or a /Properties resource by name.
  Object membership_of(const Object& operand, const Object& resources) {
    if (operand.dictionary() != nullptr) {
      return operand;
    }
    const std::string* name = operand.name();
    const Dictionary* entries = resources.dictionary();
    if (name == nullptr || entries == nullptr) {
      return {};
    }
    const Object properties = graph_.resolve(entries->get("Properties"));
    const Dictionary* names = properties.dictionary();
    return names != nullptr ? names->get(*name) : Object();
  }

  // The displacement a text-showing operation's glyphs apply, as the number
  // of a TJ that applies it; no value when it cannot be computed.
  std::optional<double> adjustment(const syntax::ContentLexer::Operation& operation,
                                   const TextState& state, const Object& resources) {
    const Dictionary* entries = resources.dictionary();
    if (state.font.is_null() || state.size == 0.0 || entries == nullptr) {
      return std::nullopt;
    }
    const Object fonts = graph_.resolve(entries->get("Font"));
    const Dictionary* names = fonts.dictionary();
    const std::string* name = state.font.name();
    if (names == nullptr || name == nullptr || !names->contains(*name)) {
      return std::nullopt;
    }
    const std::shared_ptr<fonts::PdfFont> font = loader_.load(names->get(*name));
    const bool vertical = font->is_vertical();
    const double scale = vertical ? 1.0 : state.scaling / 100.0;
    if (scale == 0.0) {
      return std::nullopt;
    }
    double moved = 0.0;
    const auto show = [&](const Object::String& text) {
      for (const fonts::PdfFont::DecodedCode& code : font->decode(platform::as_bytes(text.bytes))) {
        const double width =
            vertical ? font->vertical_advance(code.code).w1 : font->width(code.code);
        const double spacing =
            state.char_spacing +
            (code.byte_length == 1 && code.code == 32 ? state.word_spacing : 0.0);
        moved += (width / 1000.0 * state.size + spacing) * scale;
      }
    };
    if (operation.op == "TJ") {
      const Object::Array* items =
          operation.operands.empty() ? nullptr : operation.operands.front().array();
      if (items == nullptr) {
        return std::nullopt;
      }
      for (const Object& item : *items) {
        if (const Object::String* text = item.string()) {
          show(*text);
        } else {
          moved -= number_of(item) / 1000.0 * state.size * scale;
        }
      }
    } else if (!operation.operands.empty() && operation.operands.back().string() != nullptr) {
      show(*operation.operands.back().string());
    } else {
      return std::nullopt;
    }
    return -moved * 1000.0 / (state.size * scale);
  }

  // What replaces an off text-showing operation: the line move of ' and ",
  // the spacing " sets, and a TJ moving the position as its glyphs did.
  std::string hidden_show(const syntax::ContentLexer::Operation& operation, const TextState& state,
                          const Object& resources) {
    std::string out;
    if (operation.op == "\"" && operation.operands.size() == 3) {
      out = ContentSyntax::number(state.word_spacing) + " Tw " +
            ContentSyntax::number(state.char_spacing) + " Tc ";
    }
    if (operation.op == "'" || operation.op == "\"") {
      out += "T* ";
    }
    const std::optional<double> moved = adjustment(operation, state, resources);
    if (moved && ContentSyntax::number(*moved) != "0") {
      out += "[" + ContentSyntax::number(*moved) + "] TJ";
    }
    while (!out.empty() && out.back() == ' ') {
      out.pop_back();
    }
    return out;
  }

  // The edits of one stream's operations, read with resources; enters every
  // form it draws and every pattern and group its resources name.
  std::map<std::size_t, std::string> walk(const text::ContentStream& stream,
                                          const Object& resources, std::size_t page, int depth) {
    std::map<std::size_t, std::string> edits;
    const Dictionary* entries = resources.dictionary();
    const Object xobjects = entries != nullptr ? graph_.resolve(entries->get("XObject")) : Object();
    if (entries != nullptr) {
      extras(*entries, resources, page, depth);
    }
    std::vector<Mark> marks;
    std::size_t off = 0;
    TextState state;
    std::vector<TextState> saved;
    // The path being built: its operations inside the off stretch, whether
    // it began there, and whether it clips.
    std::vector<std::size_t> path;
    bool path_began_off = false;
    bool path_clips = false;
    bool path_open = false;
    for (std::size_t i = 0; i < stream.operations.size(); ++i) {
      const syntax::ContentLexer::Operation& operation = stream.operations[i];
      const std::string& op = operation.op;
      const std::vector<Object>& operands = operation.operands;
      // The text state, followed in and out of the off stretch alike.
      if (op == "q") {
        saved.push_back(state);
      } else if (op == "Q" && !saved.empty()) {
        state = saved.back();
        saved.pop_back();
      } else if (op == "Tf" && operands.size() == 2) {
        state.font = operands[0];
        state.size = number_of(operands[1]);
      } else if (op == "Tc" && operands.size() == 1) {
        state.char_spacing = number_of(operands[0]);
      } else if (op == "Tw" && operands.size() == 1) {
        state.word_spacing = number_of(operands[0]);
      } else if (op == "Tz" && operands.size() == 1) {
        state.scaling = number_of(operands[0]);
      } else if (op == "\"" && operands.size() == 3) {
        state.word_spacing = number_of(operands[0]);
        state.char_spacing = number_of(operands[1]);
      }
      if (op == "BMC" || op == "BDC") {
        const bool optional = !operands.empty() && operands.front().is_name("OC");
        bool opens_off = false;
        if (off == 0 && optional && op == "BDC" && operands.size() == 2) {
          opens_off = optional_.hidden(membership_of(operands[1], resources));
        }
        marks.push_back({optional, opens_off});
        if (opens_off) {
          ++off;
          ++result_.sequences;
        }
        if (optional || off > 0) {
          edits[i] = std::string();
        }
        continue;
      }
      if (op == "EMC") {
        if (marks.empty()) {
          continue;
        }
        const Mark mark = marks.back();
        marks.pop_back();
        if (mark.optional || off > 0) {
          edits[i] = std::string();
        }
        if (mark.opens_off) {
          --off;
        }
        continue;
      }
      if ((op == "MP" || op == "DP") &&
          (off > 0 || (!operands.empty() && operands.front().is_name("OC")))) {
        edits[i] = std::string();
        continue;
      }
      if (listed(construction, op)) {
        if (!path_open) {
          path_open = true;
          path_began_off = off > 0;
          path_clips = false;
          path.clear();
        }
        if (off > 0) {
          path.push_back(i);
        }
        continue;
      }
      if (op == "W" || op == "W*") {
        path_clips = true;
        continue;
      }
      if (listed(painting, op)) {
        if (off > 0 && op == "sh") {
          edits[i] = std::string();
        } else if (off > 0 && path_open && path_began_off && !path_clips) {
          for (const std::size_t at : path) {
            edits[at] = std::string();
          }
          edits[i] = std::string();
        } else if (off > 0 && op != "n") {
          edits[i] = "n";
        }
        if (op != "sh") {
          path_open = false;
          path.clear();
        }
        continue;
      }
      if (off > 0 && (op == "Tj" || op == "TJ" || op == "'" || op == "\"")) {
        edits[i] = hidden_show(operation, state, resources);
        continue;
      }
      if (off > 0 && (op == "BI" || op == "Do")) {
        edits[i] = std::string();
        continue;
      }
      if (op == "Do" && !operands.empty()) {
        const std::string* name = operands.back().name();
        const Dictionary* names = xobjects.dictionary();
        if (name == nullptr || names == nullptr) {
          continue;
        }
        const Object& entry = names->get(*name);
        if (xobject_off(entry)) {
          edits[i] = std::string();
          ++result_.draws;
          continue;
        }
        if (const auto reference = entry.reference()) {
          const Object target = graph_.get(reference->number);
          const syntax::Stream* drawn = target.stream();
          if (drawn != nullptr && drawn->dictionary().get("Subtype").is_name("Form")) {
            form(reference->number, resources, page, depth + 1);
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

  // value (a /Resources entry) without the XObjects that are off and the
  // /Properties entries naming groups: a reference to the edited dictionary
  // (replaced under its number) or the edited dictionary itself; no value
  // when nothing goes.
  std::optional<Object> strip(const Object& value) {
    const Object resources = graph_.resolve(value);
    const Dictionary* entries = resources.dictionary();
    if (entries == nullptr) {
      return std::nullopt;
    }
    Dictionary edited = *entries;
    bool changed = false;
    for (const std::string_view key : {"XObject", "Properties"}) {
      const Object& sub_value = entries->get(key);
      const Object sub = graph_.resolve(sub_value);
      const Dictionary* names = sub.dictionary();
      if (names == nullptr) {
        continue;
      }
      Dictionary kept;
      bool dropped = false;
      for (const auto& [name, entry] : names->entries()) {
        if (key == "XObject" ? xobject_off(entry) : names_group(entry)) {
          dropped = true;
        } else {
          kept.set(name, entry);
        }
      }
      if (!dropped) {
        continue;
      }
      if (const auto reference = sub_value.reference()) {
        graph_.set(reference->number, Object::make_dictionary(std::move(kept)));
      } else {
        edited.set(std::string(key), Object::make_dictionary(std::move(kept)));
        changed = true;
      }
    }
    if (!changed) {
      return std::nullopt;
    }
    if (const auto reference = value.reference()) {
      graph_.set(reference->number, Object::make_dictionary(std::move(edited)));
      return std::nullopt;
    }
    return Object::make_dictionary(std::move(edited));
  }

  writer::ObjectGraph& graph_;
  syntax::Document& document_;
  const text::OptionalContent& optional_;
  fonts::FontLoader loader_;
  OptionalContentRemover::Result result_;
  std::vector<std::uint32_t> pages_;
  std::map<std::uint32_t, text::ContentStream> form_walks_;
  std::map<std::uint32_t, std::map<std::size_t, std::string>> form_edits_;
  std::set<std::pair<std::uint32_t, std::size_t>> entered_;
};

// The output's page objects, in order: the flat tree the page selection
// wrote.
std::vector<std::uint32_t> page_objects(writer::ObjectGraph& graph) {
  std::vector<std::uint32_t> out;
  const Object catalog = graph.get(graph.root());
  const Dictionary* entries = catalog.dictionary();
  const Object tree = entries != nullptr ? graph.resolve_entry(*entries, "Pages") : Object();
  const Dictionary* node = tree.dictionary();
  const Object kids = node != nullptr ? graph.resolve_entry(*node, "Kids") : Object();
  if (const Object::Array* list = kids.array()) {
    for (const Object& kid : *list) {
      if (const auto reference = kid.reference()) {
        out.push_back(reference->number);
      }
    }
  }
  return out;
}

}  // namespace

OptionalContentRemover::Result OptionalContentRemover::apply(writer::ObjectGraph& graph) {
  // A document without /OCProperties shows every group (ISO 32000-1 section
  // 8.11, recalled), so its content stays and only its /OC marks and
  // the groups its resources name go.
  const text::OptionalContent optional(graph.document());
  Walker walker(graph, optional);
  const std::vector<std::uint32_t> pages = page_objects(graph);
  for (std::size_t i = 0; i < pages.size(); ++i) {
    walker.page(pages[i], i);
  }
  walker.finish();
  return walker.result();
}

}  // namespace pdfkit::redact
