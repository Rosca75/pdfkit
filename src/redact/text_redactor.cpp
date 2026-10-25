// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The two halves of text redaction. add() reads each page it needs once and
// leaves behind only the page's work, so a write reads no page with the text
// layer again: apply() lexes the streams it rewrites (the same lexer over the
// same bytes gives the same operation indices the work names) and rebuilds
// the operators from the records the work kept. A form's changes are the
// union of what every page that reaches it asked, and are written once.
//
// Every rectangle is a removed glyph's box, or the drawn placeholder's,
// widened by a sixth of the glyph's size on each side, because a glyph's ink
// can leave the box its font's ascent, descent and advance give it (an
// accent, an italic overhang, a descender below a short /Descent).

#include "redact/text_redactor.hpp"

#include "platform/error.hpp"
#include "redact/content_syntax.hpp"
#include "redact/marked_content_rewriter.hpp"
#include "redact/placeholder_drawing.hpp"
#include "redact/text_surgery.hpp"
#include "syntax/content_lexer.hpp"
#include "text/content_serialiser.hpp"
#include "text/text_reader.hpp"
#include "writer/stream_copier.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <utility>

namespace pdfkit::redact {

namespace {

using Face = writer::PlaceholderFont::Face;
using Key = TextPlan::Key;
using syntax::Dictionary;
using syntax::Object;
using text::GlyphRecord;
using text::PageText;
using text::Rect;

constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
constexpr double pad_share = 1.0 / 6.0;

[[nodiscard]] Rect padded(const Rect& box, double size) {
  if (box.is_empty()) {
    return box;
  }
  const double pad = std::abs(size) * pad_share;
  return Rect::of(box.left - pad, box.bottom - pad, box.right + pad, box.top + pad);
}

[[nodiscard]] Rect glyph_box(const GlyphRecord& record) {
  Rect box = record.box;
  if (box.is_empty()) {
    box.add(record.origin.x, record.origin.y);
    box.add(record.advance_end.x, record.advance_end.y);
  }
  return padded(box, record.size);
}

// The page's reading lines, indexed by position, and the position of every
// draw index.
struct Layout {
  std::vector<const text::TextLayout::Line*> line;
  std::vector<std::size_t> position;
  // Per /ActualText span, its glyphs.
  std::map<std::size_t, std::vector<std::size_t>> span_glyphs;
};

[[nodiscard]] Layout layout_of(const PageText& page) {
  Layout out;
  out.line.assign(page.order.size(), nullptr);
  for (const auto* blocks : {&page.blocks, &page.hidden_blocks}) {
    for (const text::TextLayout::Block& block : *blocks) {
      for (const text::TextLayout::Line& line : block.lines) {
        for (std::size_t p = line.first; p < line.last && p < out.line.size(); ++p) {
          out.line[p] = &line;
        }
      }
    }
  }
  out.position.assign(page.content.glyphs.size(), none);
  for (std::size_t p = 0; p < page.order.size(); ++p) {
    if (page.order[p] < out.position.size()) {
      out.position[page.order[p]] = p;
    }
  }
  for (const GlyphRecord& record : page.content.glyphs) {
    if (record.actual_text) {
      out.span_glyphs[*record.actual_text].push_back(record.draw);
    }
  }
  return out;
}

// What one occurrence removes and draws.
struct Geometry {
  std::set<std::size_t> removed;
  PlaceholderFitting::Result fit;
  bool drawn = false;
  bool invisible = false;
  std::vector<Rect> rectangles;
};

[[nodiscard]] Geometry geometry(const PageText& page, const Layout& layout,
                                const MatchFinder::Occurrence& occurrence,
                                const std::set<std::size_t>* page_removed) {
  Geometry out;
  const std::vector<GlyphRecord>& glyphs = page.content.glyphs;
  std::vector<std::size_t> todo;
  for (std::size_t p = occurrence.first; p < occurrence.last && p < page.order.size(); ++p) {
    todo.push_back(page.order[p]);
  }
  while (!todo.empty()) {
    const std::size_t draw = todo.back();
    todo.pop_back();
    if (draw >= glyphs.size() || !out.removed.insert(draw).second) {
      continue;
    }
    const GlyphRecord& record = glyphs[draw];
    todo.insert(todo.end(), record.duplicates.begin(), record.duplicates.end());
    if (record.duplicate_of) {
      todo.push_back(*record.duplicate_of);
    }
    if (record.actual_text) {
      if (const auto found = layout.span_glyphs.find(*record.actual_text);
          found != layout.span_glyphs.end()) {
        todo.insert(todo.end(), found->second.begin(), found->second.end());
      }
    }
  }
  // Boxes per reading line, keyed by the line's first position (a pointer key
  // would order them by address).
  std::map<std::size_t, Rect> per_line;
  for (const std::size_t draw : out.removed) {
    const std::size_t p = layout.position[draw];
    const text::TextLayout::Line* line = p == none ? nullptr : layout.line[p];
    per_line[line == nullptr ? none : line->first].add(glyph_box(glyphs[draw]));
  }
  for (const auto& [line, box] : per_line) {
    if (!box.is_empty()) {
      out.rectangles.push_back(box);
    }
  }
  // The first line: the line of the occurrence's first glyph.
  const text::TextLayout::Line* first = nullptr;
  for (std::size_t p = occurrence.first;
       p < occurrence.last && p < layout.line.size() && first == nullptr; ++p) {
    first = layout.line[p];
  }
  if (first == nullptr || occurrence.placeholder.empty()) {
    return out;
  }
  std::vector<const GlyphRecord*> removed;
  std::vector<const GlyphRecord*> kept;
  for (std::size_t p = first->first; p < first->last && p < page.order.size(); ++p) {
    const GlyphRecord& record = glyphs[page.order[p]];
    if (out.removed.contains(record.draw)) {
      removed.push_back(&record);
    } else if (page_removed == nullptr || !page_removed->contains(record.draw)) {
      kept.push_back(&record);
    }
  }
  if (removed.empty()) {
    return out;
  }
  out.fit = PlaceholderFitting::fit(removed, kept, occurrence.placeholder);
  out.drawn = out.fit.anchor != nullptr;
  out.invisible = std::ranges::all_of(removed, [](const GlyphRecord* record) {
    return record->render_mode == 3 || record->render_mode == 7;
  });
  if (out.drawn) {
    out.rectangles.push_back(padded(out.fit.box, out.fit.anchor->size));
  }
  return out;
}

// The resource dictionary a walk reads: the innermost form on its path that
// has resources of its own, else the page's (form 0).
[[nodiscard]] TextPlan::Holder holder_of(syntax::Document& document,
                                         const text::PageContent& content, std::size_t walk) {
  const std::vector<std::uint32_t>& path = content.streams[walk].form_path;
  for (const std::uint32_t number : std::views::reverse(path)) {
    const Object form = document.object({number, 0});
    if (const syntax::Stream* stream = form.stream();
        stream != nullptr &&
        document.resolve_entry(stream->dictionary(), "Resources").dictionary() != nullptr) {
      return number;
    }
  }
  return 0;
}

[[nodiscard]] TextPlan::Target& target_of(TextPlan::PageWork& work,
                                          const text::PageContent& content, std::size_t walk) {
  const std::uint32_t form = content.streams[walk].form;
  return form == 0 ? work.content : work.forms[form];
}

void note_operation(TextPlan::Target& target, const text::PageContent& content,
                    const GlyphRecord& record) {
  if (target.operations.contains(record.operation)) {
    return;
  }
  std::vector<GlyphRecord>& records = target.operations[record.operation];
  for (const GlyphRecord& other : content.glyphs) {
    if (other.stream == record.stream && other.operation == record.operation) {
      records.push_back(other);
    }
  }
}

[[nodiscard]] std::size_t insertion_point(const text::ContentStream& stream, std::size_t from) {
  for (std::size_t i = from + 1; i < stream.operations.size(); ++i) {
    if (stream.operations[i].op == "ET") {
      return i;
    }
  }
  return stream.operations.empty() ? 0 : stream.operations.size() - 1;
}

// The resource entry of category and name as written in holder's resources.
[[nodiscard]] Object resource_entry(syntax::Document& document, const syntax::Page& page,
                                    TextPlan::Holder holder, std::string_view category,
                                    const std::string& name) {
  Object resources = page.resources;
  if (holder != 0) {
    const Object form = document.object({holder, 0});
    if (const syntax::Stream* stream = form.stream()) {
      resources = stream->dictionary().get("Resources");
    }
  }
  const Object resolved = document.resolve(resources);
  if (const Dictionary* entries = resolved.dictionary()) {
    const Object group = document.resolve_entry(*entries, category);
    if (const Dictionary* items = group.dictionary()) {
      if (const Object* value = items->find(name)) {
        return *value;
      }
    }
  }
  return {};
}

// For each operation of a stream, the EMC that closes it when it opens
// marked content (BMC or BDC), else the number of operations; one pass with a
// stack, so a page of many spans costs one walk of its operations.
std::vector<std::size_t> closings(const text::ContentStream& stream) {
  const std::size_t count = stream.operations.size();
  std::vector<std::size_t> out(count, count);
  std::vector<std::size_t> open;
  for (std::size_t i = 0; i < count; ++i) {
    const std::string& op = stream.operations[i].op;
    if (op == "BMC" || op == "BDC") {
      open.push_back(i);
    } else if (op == "EMC" && !open.empty()) {
      out[open.back()] = i;
      open.pop_back();
    }
  }
  return out;
}

// Rewrites every marked-content property list of the page (BDC and DP,
// inline or through /Properties) that names a redacted string or covers
// removed glyphs (MarkedContentRewriter).
void rewrite_properties(syntax::Document& document, const PageText& page,
                        const std::set<std::size_t>& removed,
                        const std::map<std::size_t, std::string>& placeholder_of,
                        const std::vector<MatchFinder::Match>& needles, TextPlan::PageWork& work) {
  const text::PageContent& content = page.content;
  if (needles.empty() && removed.empty()) {
    return;
  }
  // Per walk, its glyphs by operation, for the glyphs a span covers.
  std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> by_walk;
  for (const GlyphRecord& record : content.glyphs) {
    by_walk[record.stream].emplace_back(record.operation, record.draw);
  }
  for (auto& [walk, items] : by_walk) {
    std::ranges::stable_sort(items, {}, &std::pair<std::size_t, std::size_t>::first);
  }
  const syntax::Page& source = document.pages().at(content.page - 1);
  const auto resolve = [&document](const Object& value) {
    return document.resolve(value);
  };
  std::set<std::pair<std::uint32_t, std::size_t>> done;
  for (std::size_t walk = 0; walk < content.streams.size(); ++walk) {
    const text::ContentStream& stream = content.streams[walk];
    std::vector<std::size_t> ends;
    for (std::size_t index = 0; index < stream.operations.size(); ++index) {
      const syntax::ContentLexer::Operation& marked = stream.operations[index];
      if ((marked.op != "BDC" && marked.op != "DP") || marked.operands.size() < 2) {
        continue;
      }
      // A form drawn twice is two walks of one stream: its lists once.
      if (stream.form != 0 && !done.insert({stream.form, index}).second) {
        continue;
      }
      bool all = true;
      std::optional<std::string> whole;
      if (marked.op == "BDC") {
        if (ends.empty()) {
          ends = closings(stream);
        }
        const auto& items = by_walk[walk];
        const auto begin =
            std::ranges::upper_bound(items, index, {}, &std::pair<std::size_t, std::size_t>::first);
        for (auto it = begin; it != items.end() && it->first < ends[index]; ++it) {
          if (removed.contains(it->second)) {
            if (!whole) {
              whole = placeholder_of.at(it->second);
            }
          } else {
            all = false;
          }
        }
      }
      const std::string* whole_text = all && whole.has_value() ? &whole.value() : nullptr;
      const Object& operand = marked.operands[1];
      if (const Dictionary* inline_properties = operand.dictionary()) {
        if (const auto rewritten =
                MarkedContentRewriter::rewrite(*inline_properties, needles, whole_text, resolve)) {
          target_of(work, content, walk).replaced[index] =
              MarkedContentRewriter::operation(marked.operands[0], *rewritten, marked.op);
        }
        continue;
      }
      const std::string* name = operand.name();
      if (name == nullptr) {
        continue;
      }
      const TextPlan::Holder holder = holder_of(document, content, walk);
      const Object entry = resource_entry(document, source, holder, "Properties", *name);
      const Object value = document.resolve(entry);
      const Dictionary* properties = value.dictionary();
      if (properties == nullptr) {
        continue;
      }
      if (const auto rewritten =
              MarkedContentRewriter::rewrite(*properties, needles, whole_text, resolve)) {
        if (const auto reference = entry.reference()) {
          work.property_objects[reference->number] = *rewritten;
        } else {
          work.properties[{holder, *name}] = *rewritten;
        }
      }
    }
  }
}

// Whether bytes spell one of the needles' byte forms.
[[nodiscard]] bool spells_bytes(std::string_view bytes,
                                const std::vector<std::vector<std::string>>& forms,
                                const std::vector<MatchFinder::Match>& needles) {
  for (std::size_t i = 0; i < needles.size(); ++i) {
    const std::string text = needles[i].folding.text.case_fold
                                 ? MatchFinder::lower_ascii(std::string(bytes))
                                 : std::string(bytes);
    for (const std::string& form : forms[i]) {
      if (text.find(form) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] std::vector<std::vector<std::string>> forms_of(
    const std::vector<MatchFinder::Match>& needles) {
  std::vector<std::vector<std::string>> forms;
  forms.reserve(needles.size());
  for (const MatchFinder::Match& needle : needles) {
    forms.push_back(MatchFinder::byte_forms(needle));
  }
  return forms;
}

// Whether a stretch of bytes outside the operations (with operations their
// lexed operations: a comment, bytes a lexer recovery skipped) spells a
// needle, and bytes with every such stretch removed.
[[nodiscard]] bool comments_spell(std::span<const std::byte> bytes,
                                  std::span<const syntax::ContentLexer::Operation> operations,
                                  const std::vector<std::vector<std::string>>& forms,
                                  const std::vector<MatchFinder::Match>& needles,
                                  std::vector<std::byte>* scrubbed) {
  bool found = false;
  std::size_t cursor = 0;
  for (const auto& [start, end] : ContentSyntax::gaps(bytes, operations)) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()) + start, end - start);
    if (!spells_bytes(text, forms, needles)) {
      continue;
    }
    found = true;
    if (scrubbed != nullptr) {
      scrubbed->insert(scrubbed->end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                       bytes.begin() + static_cast<std::ptrdiff_t>(start));
      cursor = end;
    }
  }
  if (scrubbed != nullptr) {
    scrubbed->insert(scrubbed->end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                     bytes.end());
  }
  return found;
}

// Whether a string operand of operation spells one of the needles' byte
// forms.
[[nodiscard]] bool spells(const syntax::ContentLexer::Operation& operation,
                          const std::vector<std::vector<std::string>>& forms,
                          const std::vector<MatchFinder::Match>& needles) {
  std::vector<const syntax::Object::String*> strings;
  for (const Object& operand : operation.operands) {
    if (const syntax::Object::String* string = operand.string()) {
      strings.push_back(string);
    } else if (const Object::Array* items = operand.array()) {
      for (const Object& item : *items) {
        if (const syntax::Object::String* text = item.string()) {
          strings.push_back(text);
        }
      }
    }
  }
  for (const syntax::Object::String* string : strings) {
    for (std::size_t i = 0; i < needles.size(); ++i) {
      const std::string bytes = needles[i].folding.text.case_fold
                                    ? MatchFinder::lower_ascii(string->bytes)
                                    : string->bytes;
      for (const std::string& form : forms[i]) {
        if (bytes.find(form) != std::string::npos) {
          return true;
        }
      }
    }
  }
  return false;
}

// A text-showing operator that shows no glyph (no font is current), or an
// operator Annex A does not know, whose string spells a redacted string: the
// reader drew nothing from it, a viewer with a fallback font may, and its
// bytes hold the string, so it goes, ' and " keeping their line advance and
// spacing.
void remove_unshown(const PageText& page, const std::vector<MatchFinder::Match>& needles,
                    TextPlan::PageWork& work) {
  if (needles.empty()) {
    return;
  }
  const std::vector<std::vector<std::string>> forms = forms_of(needles);
  const text::PageContent& content = page.content;
  for (std::size_t walk = 0; walk < content.streams.size(); ++walk) {
    const text::ContentStream& stream = content.streams[walk];
    if (stream.bytes != nullptr &&
        comments_spell(*stream.bytes, stream.operations, forms, needles, nullptr)) {
      target_of(work, content, walk).comments = true;
    }
  }
  std::set<std::pair<std::size_t, std::size_t>> shown;
  for (const GlyphRecord& record : content.glyphs) {
    shown.insert({record.stream, record.operation});
  }
  for (std::size_t walk = 0; walk < content.streams.size(); ++walk) {
    const text::ContentStream& stream = content.streams[walk];
    for (std::size_t index = 0; index < stream.operations.size(); ++index) {
      const syntax::ContentLexer::Operation& operation = stream.operations[index];
      const std::string& op = operation.op;
      const bool text = op == "Tj" || op == "TJ" || op == "'" || op == "\"";
      // An operator outside ISO 32000-1 Annex A does nothing a reader draws,
      // but its string operands stay in the bytes (a damaged stream lexes so).
      const bool unknown = !text && !syntax::ContentLexer::is_operator(op);
      if ((!text && !unknown) || shown.contains({walk, index}) ||
          (!spells(operation, forms, needles) && !(unknown && spells_bytes(op, forms, needles)))) {
        continue;
      }
      std::string replacement;
      if (op == "\"" && operation.operands.size() >= 2) {
        replacement = ContentSyntax::number(operation.operands[0].number().value_or(0.0)) + " Tw " +
                      ContentSyntax::number(operation.operands[1].number().value_or(0.0)) +
                      " Tc T*";
      } else if (op == "'" || op == "\"") {
        replacement = "T*";
      }
      target_of(work, content, walk).replaced[index] = replacement;
    }
  }
}

// The page's work over its merged occurrences.
[[nodiscard]] TextPlan::PageWork build_work(syntax::Document& document, const PageText& page,
                                            const std::vector<MatchFinder::Occurrence>& merged,
                                            const std::vector<MatchFinder::Match>& needles) {
  TextPlan::PageWork work;
  const Layout layout = layout_of(page);
  const text::PageContent& content = page.content;
  std::set<std::size_t> page_removed;
  std::map<std::size_t, std::string> placeholder_of;
  for (const MatchFinder::Occurrence& occurrence : merged) {
    const Geometry found = geometry(page, layout, occurrence, nullptr);
    for (const std::size_t draw : found.removed) {
      page_removed.insert(draw);
      placeholder_of.try_emplace(draw, occurrence.placeholder);
    }
  }
  for (const MatchFinder::Occurrence& occurrence : merged) {
    const Geometry found = geometry(page, layout, occurrence, &page_removed);
    for (const std::size_t draw : found.removed) {
      const GlyphRecord& record = content.glyphs[draw];
      TextPlan::Target& target = target_of(work, content, record.stream);
      target.removed.insert({record.operation, record.code_start});
      note_operation(target, content, record);
    }
    work.rectangles.insert(work.rectangles.end(), found.rectangles.begin(), found.rectangles.end());
    if (!found.drawn) {
      continue;
    }
    const GlyphRecord& anchor = *found.fit.anchor;
    const text::ContentStream& stream = content.streams[anchor.stream];
    TextPlan::Insertion insertion;
    insertion.after = insertion_point(stream, anchor.operation);
    insertion.operators = PlaceholderDrawing::operators(
        anchor, occurrence.placeholder, found.fit.face, found.fit.scale, found.invisible);
    target_of(work, content, anchor.stream)
        .insertions.try_emplace({anchor.operation, anchor.code_start}, std::move(insertion));
    const TextPlan::Holder holder = holder_of(document, content, anchor.stream);
    work.fonts.insert({holder, found.fit.face});
    if (holder == 0 && stream.form != 0) {
      for (const std::uint32_t form : stream.form_path) {
        work.form_fonts[form].insert(found.fit.face);
      }
    }
    ++work.placeholders;
  }
  rewrite_properties(document, page, page_removed, placeholder_of, needles, work);
  remove_unshown(page, needles, work);
  return work;
}

[[nodiscard]] bool work_empty(const TextPlan::PageWork& work) {
  if (!work.content.empty() || !work.property_objects.empty() || !work.properties.empty() ||
      !work.form_fonts.empty()) {
    return false;
  }
  return std::ranges::all_of(work.forms, [](const auto& entry) { return entry.second.empty(); });
}

[[nodiscard]] std::string hex_of(const GlyphRecord& record) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (std::size_t i = record.code_length; i > 0; --i) {
    const unsigned byte = (record.code >> (8U * (i - 1))) & 0xFFU;
    out += digits[byte >> 4U];
    out += digits[byte & 0x0FU];
  }
  return out;
}

void refuse(std::size_t index, const std::string& failed, const std::string& expected,
            const std::string& fix) {
  throw platform::Error(platform::ErrorCode::invalid_argument,
                        "pdfkit_redact_text match " + std::to_string(index) + " " + failed,
                        expected, fix);
}

// A stream to rewrite, lexed again from the bytes the interpreter read.
[[nodiscard]] text::ContentStream lex(const std::vector<std::vector<std::byte>>& pieces,
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

[[nodiscard]] std::vector<std::byte> rewrite(const text::ContentStream& stream,
                                             const TextPlan::Target& target,
                                             const std::vector<MatchFinder::Match>& needles,
                                             syntax::Document& document) {
  std::vector<GlyphRecord> records;
  std::set<std::size_t> removed;
  for (const auto& [index, list] : target.operations) {
    for (const GlyphRecord& original : list) {
      GlyphRecord record = original;
      record.stream = 0;
      record.draw = records.size();
      if (target.removed.contains({record.operation, record.code_start})) {
        removed.insert(record.draw);
      }
      records.push_back(std::move(record));
    }
  }
  const TextSurgery::Plan plan = TextSurgery::plan(stream, 0, records, removed);
  std::map<std::size_t, std::string> edits = plan.edits;
  for (const auto& [index, bytes] : target.replaced) {
    edits.try_emplace(index, bytes);
  }
  std::map<std::size_t, std::string> after;
  for (const auto& [key, insertion] : target.insertions) {
    after[insertion.after] += insertion.operators;
  }
  const std::vector<std::byte>& bytes = *stream.bytes;
  for (const auto& [index, text] : after) {
    if (index >= stream.operations.size()) {
      continue;
    }
    if (!edits.contains(index)) {
      const syntax::ContentLexer::Operation& operation = stream.operations[index];
      std::string original;
      for (std::size_t i = operation.start; i < operation.end && i < bytes.size(); ++i) {
        original.push_back(static_cast<char>(bytes[i]));
      }
      edits[index] = std::move(original);
    }
    edits[index] += text;
  }
  std::vector<std::byte> written = text::ContentSerialiser::write(stream, 0, {}, edits).bytes;
  if (!target.comments) {
    return written;
  }
  const std::vector<std::vector<std::byte>> pieces{written};
  const text::ContentStream again = lex(pieces, document, "a rewritten stream");
  std::vector<std::byte> scrubbed;
  (void)comments_spell(*again.bytes, again.operations, forms_of(needles), needles, &scrubbed);
  return scrubbed;
}

// resources with faces named in its /Font and properties set in its
// /Properties, as a direct dictionary.
[[nodiscard]] Object edited_resources(writer::ObjectGraph& graph, writer::PlaceholderFont& fonts,
                                      const Object& resources, const std::set<Face>& faces,
                                      const std::map<std::string, Dictionary>& properties) {
  const Object resolved = graph.resolve(resources);
  Dictionary out = resolved.dictionary() != nullptr ? *resolved.dictionary() : Dictionary();
  if (!faces.empty()) {
    const Object group = graph.resolve(out.get("Font"));
    Dictionary entries = group.dictionary() != nullptr ? *group.dictionary() : Dictionary();
    for (const Face face : faces) {
      entries.set(writer::PlaceholderFont::resource_name(face),
                  Object::make_reference(fonts.embed(face)));
    }
    out.set("Font", Object::make_dictionary(std::move(entries)));
  }
  if (!properties.empty()) {
    const Object group = graph.resolve(out.get("Properties"));
    Dictionary entries = group.dictionary() != nullptr ? *group.dictionary() : Dictionary();
    for (const auto& [name, dictionary] : properties) {
      entries.set(name, Object::make_dictionary(dictionary));
    }
    out.set("Properties", Object::make_dictionary(std::move(entries)));
  }
  return Object::make_dictionary(std::move(out));
}

}  // namespace

std::vector<TextRedactor::MatchReport> TextRedactor::add(EditPlan& plan,
                                                         std::vector<MatchFinder::Match> matches) {
  TextPlan& state = plan.text();
  syntax::Document& document = plan.document();
  const std::size_t page_count = document.pages().size();
  const std::size_t base = state.matches.size();
  bool any_search = false;
  std::set<std::uint32_t> pages;
  for (std::size_t i = 0; i < matches.size(); ++i) {
    const MatchFinder::Match& match = matches[i];
    PlaceholderFitting::require_alphabet(match.placeholder,
                                         "pdfkit_redact_text match " + std::to_string(i));
    if (match.kind == MatchFinder::Match::Kind::search) {
      any_search = true;
      continue;
    }
    if (match.fingerprint != plan.fingerprint()) {
      refuse(
          i,
          "is a glyph range taken from another document (its fingerprint " +
              (match.fingerprint.empty() ? std::string("is missing") : match.fingerprint) + ")",
          "the fingerprint " + plan.fingerprint() + " that pdfkit_text reports for this document",
          "extract the text of this document again and take the glyph range and the "
          "fingerprint from that extraction");
    }
    if (match.page < 1 || match.page > page_count) {
      refuse(i, "names page " + std::to_string(match.page),
             "a page from 1 to " + std::to_string(page_count),
             "take the page number from this document's extraction");
    }
    pages.insert(match.page);
  }
  if (any_search) {
    for (std::uint32_t p = 1; p <= page_count; ++p) {
      pages.insert(p);
    }
  }
  std::vector<MatchFinder::Match> needles = state.needles;
  for (const MatchFinder::Match& match : matches) {
    if (match.kind == MatchFinder::Match::Kind::search) {
      needles.push_back(match);
    }
  }
  std::vector<MatchReport> reports(matches.size());
  std::map<std::uint32_t, std::vector<MatchFinder::Occurrence>> found_pages;
  std::map<std::uint32_t, std::optional<TextPlan::PageWork>> works;
  std::map<std::uint32_t, std::set<std::uint32_t>> forms_drawn;
  text::TextReader reader(document);
  for (const std::uint32_t number : pages) {
    const PageText page = reader.read(number);
    std::set<std::uint32_t>& drawn_forms = forms_drawn[number];
    for (const text::ContentStream& walk : page.content.streams) {
      if (walk.form != 0) {
        drawn_forms.insert(walk.form);
      }
    }
    MatchFinder finder(page);
    std::vector<MatchFinder::Occurrence> found;
    for (std::size_t i = 0; i < matches.size(); ++i) {
      const MatchFinder::Match& match = matches[i];
      if (match.kind == MatchFinder::Match::Kind::search) {
        std::vector<MatchFinder::Occurrence> here = finder.find(match, base + i);
        // The codes that spell the string where the text does not read it.
        for (const MatchFinder::Occurrence& coded : finder.find_codes(match, base + i)) {
          const bool read = std::ranges::any_of(here, [&coded](const auto& other) {
            return other.first < coded.last && coded.first < other.last;
          });
          if (!read) {
            here.push_back(coded);
          }
        }
        found.insert(found.end(), here.begin(), here.end());
        continue;
      }
      if (match.page != number) {
        continue;
      }
      if (match.first >= match.last || match.last > page.order.size()) {
        refuse(
            i,
            "is the glyph range [" + std::to_string(match.first) + ", " +
                std::to_string(match.last) + ") of page " + std::to_string(number),
            "a non-empty range within the page's " + std::to_string(page.order.size()) + " glyphs",
            "take the range from the glyphs of this page's extraction");
      }
      found.push_back({base + i, number, match.first, match.last, match.placeholder});
      MatchFinder::Match needle;
      needle.search = finder.text(match.first, match.last);
      needle.placeholder = match.placeholder;
      needles.push_back(std::move(needle));
    }
    const Layout layout = layout_of(page);
    for (const MatchFinder::Occurrence& occurrence : found) {
      const Geometry shape = geometry(page, layout, occurrence, nullptr);
      Found entry;
      entry.page = number;
      entry.first = occurrence.first;
      entry.last = occurrence.last;
      entry.text = finder.text(occurrence.first, occurrence.last);
      entry.rectangles = shape.rectangles;
      entry.drawn = shape.drawn;
      entry.fitting = shape.fit.outcome;
      entry.scale = shape.fit.scale;
      std::uint32_t font = 0;
      for (const std::size_t draw : shape.removed) {
        const GlyphRecord& record = page.content.glyphs[draw];
        if (entry.codes.empty() || record.font_object != font) {
          entry.codes.emplace_back();
          font = record.font_object;
        }
        entry.codes.back() += hex_of(record);
      }
      reports[occurrence.match - base].occurrences.push_back(std::move(entry));
    }
    std::vector<MatchFinder::Occurrence> all;
    if (const auto earlier = state.occurrences.find(number); earlier != state.occurrences.end()) {
      all = earlier->second;
    }
    all.insert(all.end(), found.begin(), found.end());
    TextPlan::PageWork work = build_work(document, page, MatchFinder::merge(all), needles);
    if (!work_empty(work)) {
      works[number] = std::move(work);
    } else {
      works[number] = std::nullopt;
    }
    if (!found.empty()) {
      found_pages[number] = std::move(found);
    }
  }
  // Every check has passed: record.
  for (MatchFinder::Match& match : matches) {
    state.matches.push_back(std::move(match));
  }
  state.needles = std::move(needles);
  for (auto& [number, forms] : forms_drawn) {
    state.forms_drawn[number] = std::move(forms);
  }
  state.glyph_ranges =
      state.glyph_ranges || std::ranges::any_of(state.matches, [](const MatchFinder::Match& match) {
        return match.kind == MatchFinder::Match::Kind::glyphs;
      });
  for (auto& [number, found] : found_pages) {
    std::vector<MatchFinder::Occurrence>& list = state.occurrences[number];
    list.insert(list.end(), found.begin(), found.end());
  }
  for (auto& [number, work] : works) {
    if (work) {
      state.pages[number] = std::move(*work);
    } else {
      state.pages.erase(number);
    }
  }
  return reports;
}

void TextRedactor::apply(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report) {
  TextPlan& state = plan.text();
  if (state.pages.empty()) {
    return;
  }
  syntax::Document& document = plan.document();
  // The output's page dictionaries, by source page: kid i of the page tree is
  // report.pages[i].
  std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> kept;
  {
    const Object catalog = graph.get(graph.root());
    const Dictionary* entries = catalog.dictionary();
    const Object tree = entries != nullptr ? graph.resolve_entry(*entries, "Pages") : Object();
    const Dictionary* node = tree.dictionary();
    const Object kids = node != nullptr ? graph.resolve_entry(*node, "Kids") : Object();
    if (const Object::Array* list = kids.array()) {
      for (std::size_t i = 0; i < list->size() && i < report.pages.size(); ++i) {
        if (const auto reference = (*list)[i].reference()) {
          kept[report.pages[i]] = {static_cast<std::uint32_t>(i + 1), reference->number};
        }
      }
    }
  }
  writer::PlaceholderFont fonts(graph);
  std::map<std::uint32_t, TextPlan::Target> forms;
  std::map<std::uint32_t, std::set<Face>> form_fonts;
  std::map<std::uint32_t, std::map<std::string, Dictionary>> form_properties;
  for (const auto& [number, work] : state.pages) {
    for (const auto& [form, target] : work.forms) {
      forms[form].merge(target);
    }
    for (const auto& [holder, face] : work.fonts) {
      if (holder != 0) {
        form_fonts[holder].insert(face);
      }
    }
    for (const auto& [where, dictionary] : work.properties) {
      if (where.first != 0) {
        form_properties[where.first][where.second] = dictionary;
      }
    }
    for (const auto& [object, dictionary] : work.property_objects) {
      graph.set(object, Object::make_dictionary(dictionary));
    }
  }
  std::map<std::uint32_t, std::set<Face>> page_form_fonts;
  for (const auto& [number, work] : state.pages) {
    for (const auto& [form, faces] : work.form_fonts) {
      page_form_fonts[form].insert(faces.begin(), faces.end());
    }
  }
  // A form without resources of its own reads the resources of the page
  // that draws it, so every kept page naming it gains its faces.
  std::map<std::uint32_t, std::set<Face>> drawn_faces;
  if (!page_form_fonts.empty()) {
    for (const auto& [source, where] : kept) {
      const Object value = graph.get(where.second);
      const Dictionary* page = value.dictionary();
      const Object resources = page != nullptr ? graph.resolve_entry(*page, "Resources") : Object();
      const Dictionary* entries = resources.dictionary();
      const Object xobjects =
          entries != nullptr ? graph.resolve_entry(*entries, "XObject") : Object();
      if (const Dictionary* names = xobjects.dictionary()) {
        for (const auto& [name, entry] : names->entries()) {
          if (const auto reference = entry.reference()) {
            if (const auto faces = page_form_fonts.find(reference->number);
                faces != page_form_fonts.end()) {
              drawn_faces[source].insert(faces->second.begin(), faces->second.end());
            }
          }
        }
      }
    }
  }
  std::set<std::uint32_t> pages_to_edit;
  for (const auto& [number, work] : state.pages) {
    pages_to_edit.insert(number);
  }
  for (const auto& [number, faces] : drawn_faces) {
    pages_to_edit.insert(number);
  }
  const TextPlan::PageWork no_work;
  for (const std::uint32_t number : pages_to_edit) {
    const auto found = kept.find(number);
    if (found == kept.end()) {
      continue;
    }
    const auto listed = state.pages.find(number);
    const TextPlan::PageWork& work = listed != state.pages.end() ? listed->second : no_work;
    const auto [output_page, object] = found->second;
    const Object value = graph.get(object);
    if (value.dictionary() == nullptr) {
      continue;
    }
    Dictionary page = *value.dictionary();
    if (!work.content.empty()) {
      const syntax::Page& source = document.pages().at(number - 1);
      std::vector<std::vector<std::byte>> pieces;
      const Object contents = document.resolve(source.contents);
      const auto add_piece = [&](const Object& item) {
        const Object resolved = document.resolve(item);
        if (const syntax::Stream* stream = resolved.stream()) {
          pieces.push_back(document.decode(*stream, number).bytes);
        }
      };
      if (const Object::Array* list = contents.array()) {
        for (const Object& item : *list) {
          add_piece(item);
        }
      } else {
        add_piece(contents);
      }
      const text::ContentStream stream = lex(pieces, document, "page " + std::to_string(number));
      const std::vector<std::byte> bytes = rewrite(stream, work.content, state.needles, document);
      page.set("Contents", Object::make_reference(graph.add(Object::make_stream(
                               writer::StreamCopier::compress(Dictionary(), bytes)))));
    }
    std::set<Face> faces;
    for (const auto& [holder, face] : work.fonts) {
      if (holder == 0) {
        faces.insert(face);
      }
    }
    if (const auto drawn = drawn_faces.find(number); drawn != drawn_faces.end()) {
      faces.insert(drawn->second.begin(), drawn->second.end());
    }
    std::map<std::string, Dictionary> properties;
    for (const auto& [where, dictionary] : work.properties) {
      if (where.first == 0) {
        properties[where.second] = dictionary;
      }
    }
    if (!faces.empty() || !properties.empty()) {
      page.set("Resources",
               edited_resources(graph, fonts, page.get("Resources"), faces, properties));
    }
    graph.set(object, Object::make_dictionary(std::move(page)));
    writer::WriteReport::Rectangles changed;
    changed.page = output_page;
    changed.source_page = number;
    changed.placeholders = work.placeholders;
    for (const Rect& box : work.rectangles) {
      changed.boxes.push_back({box.left, box.bottom, box.right, box.top});
    }
    if (listed != state.pages.end()) {
      report.rectangles.push_back(std::move(changed));
    }
  }
  std::set<std::uint32_t> touched;
  for (const auto& [form, target] : forms) {
    if (!target.empty()) {
      touched.insert(form);
    }
  }
  for (const auto& entry : form_fonts) {
    touched.insert(entry.first);
  }
  for (const auto& entry : form_properties) {
    touched.insert(entry.first);
  }
  for (const std::uint32_t form : touched) {
    const Object value = graph.get(form);
    const syntax::Stream* stream = value.stream();
    if (stream == nullptr) {
      continue;
    }
    Dictionary dictionary = stream->dictionary();
    const auto faces = form_fonts.find(form);
    const auto properties = form_properties.find(form);
    if (faces != form_fonts.end() || properties != form_properties.end()) {
      dictionary.set("Resources",
                     edited_resources(graph, fonts, dictionary.get("Resources"),
                                      faces != form_fonts.end() ? faces->second : std::set<Face>(),
                                      properties != form_properties.end()
                                          ? properties->second
                                          : std::map<std::string, Dictionary>()));
    }
    const auto target = forms.find(form);
    if (target == forms.end() || target->second.empty()) {
      graph.set(form, Object::make_stream(stream->with_dictionary(std::move(dictionary))));
      continue;
    }
    const std::vector<std::vector<std::byte>> pieces{document.decode(*stream).bytes};
    const text::ContentStream walk = lex(pieces, document, "form " + std::to_string(form));
    const std::vector<std::byte> bytes = rewrite(walk, target->second, state.needles, document);
    for (const std::string_view key : {"Filter", "DecodeParms", "Length", "DL"}) {
      dictionary.erase(key);
    }
    graph.set(form,
              Object::make_stream(writer::StreamCopier::compress(std::move(dictionary), bytes)));
  }
  std::ranges::stable_sort(report.rectangles, {}, &writer::WriteReport::Rectangles::page);
}

}  // namespace pdfkit::redact
