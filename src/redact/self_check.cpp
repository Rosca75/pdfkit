// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The search, surface by surface. The streams are classified in one pass
// over every object first: what a font program, a font's CMap, an ICC
// profile or a content stream is follows from the key or the array that
// names it, not from the stream's own dictionary, so the referrers are read
// before any stream is scanned. The raw scan folds ASCII letters to lower
// case when the string's folding folds case; it applies no other folding,
// because it looks for the bytes a producer would have written. A located
// match is verified on its output page alone: the glyphs "there" are those
// whose box centre lies in the box of a glyph the match removed, because a
// kept neighbour's box touches a removed glyph's but its centre lies
// outside, and a glyph drawn over the removed one (a second text layer)
// lies inside.

#include "redact/self_check.hpp"

#include "platform/unicode/folding.hpp"
#include "redact/content_syntax.hpp"
#include "syntax/content_lexer.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/stream.hpp"
#include "text/page_text.hpp"
#include "text/text_reader.hpp"
#include "text/text_string.hpp"
#include "writer/metadata_writer.hpp"
#include "writer/placeholder_font.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace pdfkit::redact {

namespace {

using syntax::Dictionary;
using syntax::Object;
using TextFolding = platform::unicode::Folding;

constexpr int max_depth = 64;
constexpr std::size_t listed = 5;

enum class Kind : std::uint8_t { scan, content, skip };

// Every placeholder cut out of text (UTF-8).
[[nodiscard]] std::string without(std::string text, std::span<const std::string> placeholders) {
  for (const std::string& placeholder : placeholders) {
    if (placeholder.empty()) {
      continue;
    }
    std::size_t at = 0;
    while ((at = text.find(placeholder, at)) != std::string::npos) {
      text.replace(at, placeholder.size(), " ");
      at += 1;
    }
  }
  return text;
}

[[nodiscard]] bool scan(std::string bytes, const std::vector<std::string>& wanted, bool fold_case,
                        std::span<const std::string> placeholders) {
  bytes = without(std::move(bytes), placeholders);
  if (fold_case) {
    bytes = MatchFinder::lower_ascii(std::move(bytes));
  }
  return std::ranges::any_of(wanted, [&bytes](const std::string& form) {
    return !form.empty() && bytes.find(form) != std::string::npos;
  });
}

// The character reference or predefined entity at xml[at] ('&'), as UTF-8,
// and the index of its ';'; no value when it is neither.
[[nodiscard]] std::optional<std::pair<std::string, std::size_t>> entity_at(std::string_view xml,
                                                                           std::size_t at) {
  const std::size_t end = xml.find(';', at);
  if (end == std::string_view::npos || end - at > 12) {
    return std::nullopt;
  }
  const std::string_view name = xml.substr(at + 1, end - at - 1);
  char32_t code = 0;
  if (name == "amp") {
    code = U'&';
  } else if (name == "lt") {
    code = U'<';
  } else if (name == "gt") {
    code = U'>';
  } else if (name == "quot") {
    code = U'"';
  } else if (name == "apos") {
    code = U'\'';
  } else if (name.size() > 1 && name[0] == '#') {
    const bool hex = name[1] == 'x' || name[1] == 'X';
    const std::string_view digits = name.substr(hex ? 2 : 1);
    if (digits.empty()) {
      return std::nullopt;
    }
    for (const char digit : digits) {
      const int value = digit >= '0' && digit <= '9'   ? digit - '0'
                        : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                        : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                                       : 99;
      if (value >= (hex ? 16 : 10) || code > 0x10FFFF) {
        return std::nullopt;
      }
      code = (code * (hex ? 16U : 10U)) + static_cast<char32_t>(value);
    }
  }
  if (code == 0 || code > 0x10FFFF) {
    return std::nullopt;
  }
  return std::make_pair(TextFolding::encode(std::u32string(1, code)), end);
}

[[nodiscard]] bool name_start(char value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_' ||
         value == ':';
}

[[nodiscard]] bool name_char(char value) noexcept {
  return name_start(value) || (value >= '0' && value <= '9') || value == '-' || value == '.';
}

// The text of an XML packet with its markup's names left out: every element
// name and every attribute name goes, every character datum and attribute
// value stays, entities read. It does not track quotes, so a packet whose
// quotes do not balance (a byte-order mark that lost its closing quote) loses
// no value.
[[nodiscard]] std::string xml_text(std::string_view xml) {
  std::string out;
  std::size_t i = 0;
  while (i < xml.size()) {
    const char value = xml[i];
    if (value == '<') {
      ++i;
      while (i < xml.size() && (xml[i] == '/' || xml[i] == '?' || xml[i] == '!')) {
        ++i;
      }
      while (i < xml.size() && name_char(xml[i])) {
        ++i;
      }
      out += ' ';
      continue;
    }
    if (name_start(value)) {
      std::size_t end = i;
      while (end < xml.size() && name_char(xml[end])) {
        ++end;
      }
      std::size_t next = end;
      while (next < xml.size() &&
             (xml[next] == ' ' || xml[next] == '\t' || xml[next] == '\n' || xml[next] == '\r')) {
        ++next;
      }
      if (next < xml.size() && xml[next] == '=') {
        const std::string_view name = xml.substr(i, end - i);
        out += ' ';
        i = next + 1;
        // A namespace declaration's value is a URI of the format's
        // vocabulary ("http://ns.adobe.com/xap/1.0/"), not the document's
        // text, so it is skipped with its quotes.
        if (name == "xmlns" || name.starts_with("xmlns:")) {
          while (i < xml.size() && xml[i] != '"' && xml[i] != '\'' && xml[i] != '>') {
            ++i;
          }
          if (i < xml.size() && xml[i] != '>') {
            const std::size_t close = xml.find(xml[i], i + 1);
            i = close == std::string_view::npos ? xml.size() : close + 1;
          }
        }
      } else {
        out.append(xml.substr(i, end - i));
        i = end;
      }
      continue;
    }
    if (value == '&') {
      if (const auto entity = entity_at(xml, i)) {
        out += entity->first;
        i = entity->second + 1;
        continue;
      }
    }
    if (value == '>' || value == '"' || value == '\'' || value == '=' || value == '/' ||
        value == '?') {
      out += ' ';
    } else {
      out += value;
    }
    ++i;
  }
  return out;
}

// The surfaces an original value of a rewritten metadata field is searched
// in.
[[nodiscard]] bool metadata_surface(std::string_view surface) noexcept {
  return surface == "info" || surface == "xmp" || surface == "string";
}

class Search {
 public:
  Search(std::span<const MatchFinder::Match> needles, std::span<const MatchFinder::Match> metadata,
         std::span<const std::string> placeholders, std::span<const std::string> written,
         SelfCheck::Result& result)
      : needles_(needles),
        metadata_(metadata),
        placeholders_(placeholders),
        written_(written),
        result_(result) {
    for (const MatchFinder::Match& needle : needles) {
      forms_.push_back(MatchFinder::byte_forms(needle));
    }
  }

  // A text surface (UTF-8).
  void text(const std::string& value, std::optional<std::uint32_t> page, std::string_view surface,
            std::uint32_t object) {
    if (value.empty()) {
      return;
    }
    const std::u32string cut = TextFolding::decode(without(value, placeholders_));
    for (const MatchFinder::Match& needle : needles_) {
      if (!MatchFinder::ranges_in(cut, needle).empty()) {
        add(needle, page, surface, object);
      }
    }
    if (metadata_surface(surface) && !metadata_.empty()) {
      // The values the caller wrote are cut out too, as the placeholders
      // are: "[_5_]" written over an original "5" is the caller's own value.
      const std::u32string fresh =
          TextFolding::decode(without(without(value, placeholders_), written_));
      for (const MatchFinder::Match& needle : metadata_) {
        if (!MatchFinder::ranges_in(fresh, needle).empty()) {
          add(needle, page, surface, object);
        }
      }
    }
  }

  // Raw bytes of a stream or of a string operand.
  void raw(const std::string& bytes, std::optional<std::uint32_t> page, std::uint32_t object) {
    if (bytes.empty()) {
      return;
    }
    for (std::size_t i = 0; i < needles_.size(); ++i) {
      if (scan(bytes, forms_[i], needles_[i].folding.text.case_fold, placeholders_)) {
        add(needles_[i], page, "stream", object);
      }
    }
  }

  void add(const MatchFinder::Match& needle, std::optional<std::uint32_t> page,
           std::string_view surface, std::uint32_t object) {
    add(needle.search, page, surface, object);
  }

  void add(const std::string& string, std::optional<std::uint32_t> page, std::string_view surface,
           std::uint32_t object) {
    const std::tuple<std::string, std::optional<std::uint32_t>, std::string, std::uint32_t> key{
        string, page, std::string(surface), object};
    if (seen_.insert(key).second) {
      result_.survivors.push_back({string, page, std::string(surface), object});
    }
  }

 private:
  std::span<const MatchFinder::Match> needles_;
  std::span<const MatchFinder::Match> metadata_;
  std::span<const std::string> placeholders_;
  std::span<const std::string> written_;
  std::vector<std::vector<std::string>> forms_;
  SelfCheck::Result& result_;
  std::set<std::tuple<std::string, std::optional<std::uint32_t>, std::string, std::uint32_t>> seen_;
};

[[nodiscard]] bool font_dictionary(const Dictionary& dictionary) {
  const Object& type = dictionary.get("Type");
  return type.is_name("Font") || type.is_name("FontDescriptor") ||
         (dictionary.contains("Registry") && dictionary.contains("Ordering"));
}

void strings_of(const Object& value, int depth, std::vector<std::string>& out) {
  if (depth > max_depth) {
    return;
  }
  if (const Object::String* string = value.string()) {
    out.push_back(text::TextString::to_utf8(string->bytes));
  } else if (const Object::Array* items = value.array()) {
    for (const Object& item : *items) {
      strings_of(item, depth + 1, out);
    }
  } else if (const Dictionary* dictionary = value.dictionary_or_stream_dictionary()) {
    if (font_dictionary(*dictionary)) {
      return;
    }
    for (const auto& [key, item] : dictionary->entries()) {
      strings_of(item, depth + 1, out);
    }
  }
}

// The raw strings among a content stream's operands.
void operand_strings(const Object& value, int depth, std::string& out) {
  if (depth > max_depth) {
    return;
  }
  if (const Object::String* string = value.string()) {
    out += string->bytes;
    out += '\n';
  } else if (const Object::Array* items = value.array()) {
    for (const Object& item : *items) {
      operand_strings(item, depth + 1, out);
    }
  } else if (const Dictionary* dictionary = value.dictionary()) {
    for (const auto& [key, item] : dictionary->entries()) {
      operand_strings(item, depth + 1, out);
    }
  }
}

// Whether the centre of a glyph's box lies in one of boxes.
[[nodiscard]] bool there(const text::GlyphRecord& record, std::span<const text::Rect> boxes) {
  text::Rect box = record.box;
  if (box.is_empty()) {
    box.add(record.origin.x, record.origin.y);
    box.add(record.advance_end.x, record.advance_end.y);
  }
  if (box.is_empty()) {
    return false;
  }
  const double x = (box.left + box.right) / 2.0;
  const double y = (box.bottom + box.top) / 2.0;
  return std::ranges::any_of(boxes, [x, y](const text::Rect& hole) {
    return x >= hole.left && x <= hole.right && y >= hole.bottom && y <= hole.top;
  });
}

[[nodiscard]] std::string hex_of(const text::GlyphRecord& record) {
  constexpr std::string_view hex = "0123456789abcdef";
  std::string out;
  for (std::size_t i = record.code_length; i > 0; --i) {
    const unsigned byte = (record.code >> (8U * (i - 1))) & 0xFFU;
    out += hex[byte >> 4U];
    out += hex[byte & 0x0FU];
  }
  return out;
}

// Per walk, the index one past the EMC closing each BDC or BMC (the end of
// the walk when none closes it).
[[nodiscard]] std::vector<std::size_t> closings(const text::ContentStream& stream) {
  const std::size_t size = stream.operations.size();
  std::vector<std::size_t> out(size, size);
  std::vector<std::size_t> open;
  for (std::size_t index = 0; index < size; ++index) {
    const std::string& op = stream.operations[index].op;
    if (op == "BDC" || op == "BMC") {
      open.push_back(index);
    } else if (op == "EMC" && !open.empty()) {
      out[open.back()] = index + 1;
      open.pop_back();
    }
  }
  return out;
}

// The located check of docs/DESIGN.md section 18 over one output page.
void check_located(const text::PageText& page, MatchFinder& finder,
                   std::span<const SelfCheck::Located* const> located,
                   std::span<const std::string> placeholders,
                   const std::set<std::string_view>& placeholder_fonts, Search& search) {
  const std::vector<text::GlyphRecord>& glyphs = page.content.glyphs;
  const auto placeholder = [&placeholder_fonts](const text::GlyphRecord& record) {
    return record.font != nullptr && placeholder_fonts.contains(record.font->name());
  };
  std::vector<std::vector<std::size_t>> ends(page.content.streams.size());
  for (const SelfCheck::Located* entry : located) {
    const std::uint32_t number = page.content.page;
    MatchFinder::Match needle;
    needle.search = entry->text;
    bool survives = false;
    // The text, in the reading order and every other order, with a glyph
    // there.
    std::vector<MatchFinder::Occurrence> occurrences = finder.find(needle, 0);
    for (MatchFinder::Occurrence& other : finder.find_orders(needle, 0, occurrences)) {
      occurrences.push_back(std::move(other));
    }
    for (const MatchFinder::Occurrence& found : occurrences) {
      for (std::size_t p = found.first; p < found.last && p < page.order.size() && !survives; ++p) {
        const text::GlyphRecord& record = glyphs[page.order[p]];
        survives = !record.text.empty() && !record.is_space() && !placeholder(record) &&
                   there(record, entry->boxes);
      }
    }
    // The removed codes in a string operator drawing a glyph there: the
    // codes of its glyphs there, in draw order, hold a removed run.
    std::map<std::pair<std::size_t, std::size_t>, std::string> drawn;
    std::set<std::size_t> spans;
    for (const text::GlyphRecord& record : glyphs) {
      if (!there(record, entry->boxes)) {
        continue;
      }
      if (!placeholder(record)) {
        drawn[{record.stream, record.operation}] += hex_of(record);
      }
      if (record.actual_text) {
        spans.insert(*record.actual_text);
      }
      // Every span whose BDC encloses the glyph in its walk.
      for (std::size_t s = 0; s < page.content.spans.size(); ++s) {
        const text::MarkedSpan& span = page.content.spans[s];
        if (span.stream != record.stream || span.operation > record.operation) {
          continue;
        }
        std::vector<std::size_t>& closing = ends[span.stream];
        if (closing.empty()) {
          closing = closings(page.content.streams[span.stream]);
        }
        if (span.operation < closing.size() && record.operation < closing[span.operation]) {
          spans.insert(s);
        }
      }
    }
    for (const auto& [where, codes] : drawn) {
      for (const std::string& run : entry->codes) {
        for (std::size_t at = codes.find(run); !run.empty() && at != std::string::npos && !survives;
             at = codes.find(run, at + 1)) {
          survives = at % 2 == 0;
        }
      }
    }
    for (const std::size_t s : spans) {
      const text::MarkedSpan& span = page.content.spans[s];
      for (const std::optional<std::string>* value :
           {&span.actual_text, &span.alt, &span.expansion}) {
        if (*value &&
            !MatchFinder::ranges_in(TextFolding::decode(without(**value, placeholders)), needle)
                 .empty()) {
          survives = true;
        }
      }
    }
    if (survives) {
      search.add(entry->text, number, "location", 0);
    }
  }
}

}  // namespace

SelfCheck::Result SelfCheck::run(platform::ByteSpan pdf, const syntax::Limits& limits,
                                 std::span<const MatchFinder::Match> needles,
                                 std::span<const std::string> placeholders,
                                 const std::set<std::uint32_t>* pages) {
  return run(pdf, limits, Needles{needles, {}, {}, {}}, placeholders, pages);
}

SelfCheck::Result SelfCheck::run(platform::ByteSpan pdf, const syntax::Limits& limits,
                                 const Needles& requests, std::span<const std::string> placeholders,
                                 const std::set<std::uint32_t>* pages) {
  Result result;
  const std::span<const MatchFinder::Match> needles = requests.values;
  std::vector<MatchFinder::Match> originals;
  for (const std::string& original : requests.metadata) {
    MatchFinder::Match needle;
    needle.search = original;
    originals.push_back(std::move(needle));
  }
  result.located = requests.located.size();
  result.metadata_values = originals.size();
  result.strings = needles.size() + result.located + result.metadata_values;
  std::unique_ptr<syntax::Document> document = syntax::Document::open(pdf, limits);
  Search search(needles, originals, placeholders, requests.written, result);
  if (result.strings == 0) {
    return result;
  }
  std::map<std::uint32_t, std::vector<const Located*>> located_on;
  for (const Located& entry : requests.located) {
    located_on[entry.page].push_back(&entry);
  }
  const std::set<std::string_view> placeholder_fonts{
      writer::PlaceholderFont::metrics(writer::PlaceholderFont::Face::regular).base_font,
      writer::PlaceholderFont::metrics(writer::PlaceholderFont::Face::bold).base_font};

  // Page text and marked-content properties.
  text::TextReader reader(*document);
  result.pages = document->pages().size();
  for (const syntax::Page& source : document->pages()) {
    const auto here = located_on.find(source.number);
    const bool values = !needles.empty() && (pages == nullptr || pages->contains(source.number));
    if (!values && here == located_on.end()) {
      continue;
    }
    ++result.pages_read;
    const text::PageText page = reader.read(source.number);
    MatchFinder finder(page, {placeholder_fonts.begin(), placeholder_fonts.end()});
    if (here != located_on.end()) {
      check_located(page, finder, here->second, placeholders, placeholder_fonts, search);
    }
    if (!values) {
      continue;
    }
    for (const MatchFinder::Match& needle : needles) {
      // The reading order and every other order a reader reads the page in.
      std::vector<MatchFinder::Occurrence> occurrences = finder.find(needle, 0);
      for (MatchFinder::Occurrence& other : finder.find_orders(needle, 0, occurrences)) {
        occurrences.push_back(std::move(other));
      }
      for (const MatchFinder::Occurrence& found : occurrences) {
        bool own = false;
        for (std::size_t p = found.first; p < found.last && !own; ++p) {
          const text::GlyphRecord& record = page.content.glyphs[page.order[p]];
          own = !record.text.empty() && !record.is_space() &&
                (record.font == nullptr || !placeholder_fonts.contains(record.font->name()));
        }
        if (own) {
          search.add(needle, source.number, "text", 0);
        }
      }
    }
    for (const text::MarkedSpan& span : page.content.spans) {
      if (span.actual_text) {
        search.text(*span.actual_text, source.number, "actual_text", 0);
      }
      if (span.alt) {
        search.text(*span.alt, source.number, "alt", 0);
      }
      if (span.expansion) {
        search.text(*span.expansion, source.number, "expansion", 0);
      }
    }
  }

  // The Info dictionary and the XMP packet.
  std::uint32_t info_number = 0;
  if (const auto reference = document->trailer().get("Info").reference()) {
    info_number = reference->number;
  }
  if (const Dictionary* info = document->info().dictionary()) {
    // Every string of every entry, the strings of an array or dictionary
    // value included, because the string walk below leaves the Info
    // dictionary's own object to this surface.
    for (const auto& [key, value] : info->entries()) {
      std::vector<std::string> strings;
      strings_of(document->resolve(value), 0, strings);
      for (const std::string& string : strings) {
        search.text(string, std::nullopt, "info", info_number);
      }
    }
  }
  std::uint32_t xmp_number = 0;
  if (const Dictionary* catalog = document->catalog().dictionary()) {
    const Object& entry = catalog->get("Metadata");
    if (const auto reference = entry.reference()) {
      xmp_number = reference->number;
    }
    const Object metadata = document->resolve(entry);
    if (const syntax::Stream* stream = metadata.stream()) {
      const syntax::Filters::Result decoded = document->decode(*stream);
      const std::string packet(reinterpret_cast<const char*>(decoded.bytes.data()),
                               decoded.bytes.size());
      search.text(xml_text(packet), std::nullopt, "xmp", xmp_number);
    }
  }

  // Classify the streams by what names them.
  std::map<std::uint32_t, Kind> kinds;
  std::map<std::uint32_t, std::uint32_t> page_of;
  for (const syntax::Page& page : document->pages()) {
    const Object contents = page.contents;
    const auto mark = [&](const Object& item) {
      if (const auto reference = item.reference()) {
        kinds[reference->number] = Kind::content;
        page_of.try_emplace(reference->number, page.number);
      }
    };
    const Object resolved = document->resolve(contents);
    if (const Object::Array* list = resolved.array()) {
      for (const Object& item : *list) {
        mark(item);
      }
    } else {
      mark(contents);
    }
  }
  const auto skip = [&kinds](const Object& value) {
    if (const auto reference = value.reference()) {
      kinds[reference->number] = Kind::skip;
    }
  };
  std::vector<std::uint32_t> numbers;
  for (const auto& [number, entry] : document->entries()) {
    numbers.push_back(number);
  }
  for (const std::uint32_t number : numbers) {
    const Object value = document->object({number, 0});
    const Dictionary* dictionary = value.dictionary_or_stream_dictionary();
    if (const Object::Array* items = value.array()) {
      if (items->size() == 2 && (*items)[0].is_name("ICCBased")) {
        skip((*items)[1]);
      }
    }
    if (dictionary == nullptr) {
      continue;
    }
    for (const std::string_view key : {"FontFile", "FontFile2", "FontFile3", "ToUnicode", "CIDSet",
                                       "CIDToGIDMap", "DestOutputProfile"}) {
      skip(dictionary->get(key));
    }
    if (dictionary->get("Type").is_name("Font") || dictionary->get("Subtype").is_name("Type0")) {
      skip(dictionary->get("Encoding"));
      skip(dictionary->get("UseCMap"));
    }
    if (dictionary->contains("CharProcs")) {
      if (const Dictionary* procedures =
              document->resolve(dictionary->get("CharProcs")).dictionary()) {
        for (const auto& [name, procedure] : procedures->entries()) {
          if (const auto reference = procedure.reference()) {
            kinds.try_emplace(reference->number, Kind::content);
          }
        }
      }
    }
    for (const auto& [key, item] : dictionary->entries()) {
      if (const Object::Array* items = item.array()) {
        if (items->size() == 2 && (*items)[0].is_name("ICCBased")) {
          skip((*items)[1]);
        }
      }
      if (key == "ColorSpace") {
        if (const Dictionary* spaces = document->resolve(item).dictionary()) {
          for (const auto& [name, space] : spaces->entries()) {
            if (const Object::Array* items = document->resolve(space).array();
                items != nullptr && items->size() == 2 && (*items)[0].is_name("ICCBased")) {
              skip((*items)[1]);
            }
          }
        }
      }
    }
  }

  // String objects and streams.
  for (const std::uint32_t number : numbers) {
    const Object value = document->object({number, 0});
    if (number != info_number) {
      std::vector<std::string> strings;
      strings_of(value, 0, strings);
      for (const std::string& string : strings) {
        search.text(string, std::nullopt, "string", number);
      }
    }
    const syntax::Stream* stream = value.stream();
    if (stream == nullptr || number == xmp_number) {
      continue;
    }
    const Dictionary& dictionary = stream->dictionary();
    Kind kind = Kind::scan;
    if (const auto found = kinds.find(number); found != kinds.end()) {
      kind = found->second;
    }
    if (dictionary.get("Subtype").is_name("Image") || dictionary.contains("FunctionType") ||
        dictionary.get("Type").is_name("Metadata") || dictionary.get("Type").is_name("CMap") ||
        dictionary.get("Type").is_name("XRef") || dictionary.get("Type").is_name("ObjStm")) {
      kind = Kind::skip;
    } else if (kind == Kind::scan &&
               (dictionary.get("Subtype").is_name("Form") || dictionary.contains("PatternType"))) {
      kind = Kind::content;
    }
    if (kind == Kind::skip) {
      continue;
    }
    const syntax::Filters::Result decoded = document->decode(*stream);
    std::optional<std::uint32_t> page;
    if (const auto found = page_of.find(number); found != page_of.end()) {
      page = found->second;
    }
    if (kind == Kind::scan) {
      search.raw(
          std::string(reinterpret_cast<const char*>(decoded.bytes.data()), decoded.bytes.size()),
          page, number);
      continue;
    }
    const std::vector<std::vector<std::byte>> pieces{decoded.bytes};
    syntax::ContentLexer lexer(pieces, document->limits(), "object " + std::to_string(number));
    std::string operands;
    std::vector<syntax::ContentLexer::Operation> operations;
    while (std::optional<syntax::ContentLexer::Operation> operation = lexer.next()) {
      for (const Object& operand : operation->operands) {
        operand_strings(operand, 0, operands);
      }
      operations.push_back(std::move(*operation));
    }
    // The comments and skipped bytes too: a producer's note in a content
    // stream is in its bytes; and an operator Annex A does not know is read
    // as a word of the stream.
    const platform::ByteSpan joined = lexer.bytes();
    for (const syntax::ContentLexer::Operation& operation : operations) {
      if (!syntax::ContentLexer::is_operator(operation.op)) {
        operands += operation.op;
        operands += '\n';
      }
    }
    for (const auto& [start, end] : ContentSyntax::gaps(joined, operations)) {
      operands.append(reinterpret_cast<const char*>(joined.data()) + start, end - start);
      operands += '\n';
    }
    search.raw(operands, page, number);
  }
  return result;
}

void SelfCheck::verify(EditPlan& plan, const std::string& pdf, writer::WriteReport& report) {
  const TextPlan& text = plan.text();
  if (text.matches.empty() && plan.metadata_originals().empty()) {
    report.self_check_status = "not_run";
    report.self_check_reason = "no text redaction in the edit plan";
    return;
  }
  std::set<std::string> unique;
  for (const MatchFinder::Match& match : text.matches) {
    unique.insert(match.placeholder);
  }
  const std::vector<std::string> placeholders(unique.begin(), unique.end());
  // An original the plan also searches for with the default folding is
  // verified everywhere as a value already.
  const auto searched = [&text](const std::string& original) {
    return std::ranges::any_of(text.needles, [&original](const MatchFinder::Match& match) {
      const MatchFinder::Folding defaults;
      return match.search == original && match.folding.hyphenation == defaults.hyphenation &&
             match.folding.text.case_fold == defaults.text.case_fold &&
             match.folding.text.diacritics == defaults.text.diacritics &&
             match.folding.text.whitespace == defaults.text.whitespace;
    });
  };
  std::vector<std::string> metadata;
  for (const std::string& original : plan.metadata_originals()) {
    if (!searched(original)) {
      metadata.push_back(original);
    }
  }
  std::vector<std::string> written;
  for (const writer::MetadataWriter::Written& field :
       writer::MetadataWriter::preview(plan.metadata())) {
    for (const std::string* value : {&field.info, &field.xmp}) {
      if (!value->empty()) {
        written.push_back(*value);
      }
    }
  }
  // The output page of each source page.
  std::map<std::uint32_t, std::uint32_t> output_page;
  for (std::size_t i = 0; i < report.pages.size(); ++i) {
    output_page.try_emplace(report.pages[i], static_cast<std::uint32_t>(i + 1));
  }
  std::vector<Located> located;
  for (const TextPlan::Located& entry : text.located) {
    if (const auto kept = output_page.find(entry.page); kept != output_page.end()) {
      located.push_back({entry.text, kept->second, entry.boxes, entry.codes});
    }
  }
  // Every value was searched on every source page when its match was added,
  // so a page the redaction left as it was (no edit, no edited form drawn)
  // reads as it did then and holds no occurrence; its text is not read
  // again. The page text of every changed page and every other surface of
  // every page is.
  std::set<std::uint32_t> forms;
  for (const auto& [number, work] : text.pages) {
    for (const auto& [form, target] : work.forms) {
      if (!target.empty()) {
        forms.insert(form);
      }
    }
  }
  std::set<std::uint32_t> changed;
  for (std::size_t i = 0; i < report.pages.size(); ++i) {
    const std::uint32_t source = report.pages[i];
    bool touched = text.pages.contains(source);
    if (const auto drawn = text.forms_drawn.find(source); drawn != text.forms_drawn.end()) {
      for (const std::uint32_t form : drawn->second) {
        touched = touched || forms.contains(form);
      }
    } else {
      touched = true;
    }
    if (touched) {
      changed.insert(static_cast<std::uint32_t>(i + 1));
    }
  }
  const Result result =
      run(platform::as_bytes(pdf), plan.document().limits(),
          Needles{text.needles, located, metadata, written}, placeholders, &changed);
  report.self_check_strings = result.strings;
  report.self_check_located = result.located;
  report.self_check_metadata_values = result.metadata_values;
  if (!result.survivors.empty()) {
    throw refusal(result);
  }
  report.self_check_status = "passed";
  report.self_check_reason.clear();
}

platform::Error SelfCheck::refusal(const Result& result) {
  std::string failed = "the self-check found " + std::to_string(result.survivors.size()) +
                       " surviving occurrence" + (result.survivors.size() == 1 ? "" : "s") +
                       " of redacted text in the output: ";
  for (std::size_t i = 0; i < result.survivors.size() && i < listed; ++i) {
    const Survivor& survivor = result.survivors[i];
    if (i > 0) {
      failed += "; ";
    }
    failed += "\"" + survivor.string + "\" in the " + survivor.surface;
    if (survivor.page) {
      failed += " of page " + std::to_string(*survivor.page);
    }
    if (survivor.object != 0) {
      failed += " (object " + std::to_string(survivor.object) + ")";
    }
  }
  if (result.survivors.size() > listed) {
    failed += "; and " + std::to_string(result.survivors.size() - listed) + " more";
  }
  return {platform::ErrorCode::self_check, failed,
          "an output in which no searched string survives in any text surface (page text, "
          "marked-content properties, the Info dictionary, the XMP packet, string objects, "
          "stream bytes), no glyph range's text survives at its location, and no original "
          "value of a rewritten metadata field survives in the metadata",
          "redact the string where it survives: add a match for page text (a glyph range for "
          "text drawn over the location), set the metadata fields that hold it, or report the "
          "surface as a defect when it is drawn text"};
}

}  // namespace pdfkit::redact
