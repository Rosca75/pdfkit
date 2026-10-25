// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The search, surface by surface. The streams are classified in one pass
// over every object first: what a font program, a font's CMap, an ICC
// profile or a content stream is follows from the key or the array that
// names it, not from the stream's own dictionary, so the referrers are read
// before any stream is scanned. The raw scan folds ASCII letters to lower
// case when the string's folding folds case; it applies no other folding,
// because it looks for the bytes a producer would have written.

#include "redact/self_check.hpp"

#include "platform/unicode/folding.hpp"
#include "syntax/content_lexer.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/stream.hpp"
#include "text/text_reader.hpp"
#include "text/text_string.hpp"
#include "writer/placeholder_font.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include <map>
#include <set>
#include <string_view>

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

[[nodiscard]] char lower(char value) noexcept {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

// The byte forms of a needle the raw scan looks for.
[[nodiscard]] std::vector<std::string> forms(const MatchFinder::Match& needle) {
  std::u32string text = TextFolding::decode(needle.search);
  if (needle.folding.text.whitespace) {
    std::u32string collapsed;
    for (const char32_t value : text) {
      if (TextFolding::is_white_space(value)) {
        if (!collapsed.empty() && collapsed.back() != U' ') {
          collapsed.push_back(U' ');
        }
      } else {
        collapsed.push_back(value);
      }
    }
    while (!collapsed.empty() && collapsed.back() == U' ') {
      collapsed.pop_back();
    }
    text = std::move(collapsed);
  }
  std::vector<std::string> out;
  if (text.empty()) {
    return out;
  }
  out.push_back(TextFolding::encode(text));
  std::string latin;
  bool fits = true;
  std::string utf16;
  for (const char32_t value : text) {
    fits = fits && value < 0x100;
    latin.push_back(static_cast<char>(value & 0xFFU));
    if (value >= 0x10000) {
      const char32_t offset = value - 0x10000;
      for (const char32_t unit : {0xD800 + (offset >> 10U), 0xDC00 + (offset & 0x3FFU)}) {
        utf16.push_back(static_cast<char>(unit >> 8U));
        utf16.push_back(static_cast<char>(unit & 0xFFU));
      }
    } else {
      utf16.push_back(static_cast<char>(value >> 8U));
      utf16.push_back(static_cast<char>(value & 0xFFU));
    }
  }
  if (fits && latin != out.front()) {
    out.push_back(latin);
  }
  out.push_back(utf16);
  if (needle.folding.text.case_fold) {
    for (std::string& form : out) {
      std::ranges::transform(form, form.begin(), lower);
    }
  }
  return out;
}

[[nodiscard]] bool scan(std::string bytes, const std::vector<std::string>& wanted, bool fold_case,
                        std::span<const std::string> placeholders) {
  bytes = without(std::move(bytes), placeholders);
  if (fold_case) {
    std::ranges::transform(bytes, bytes.begin(), lower);
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

// The character data and attribute values of an XML packet, entities read.
[[nodiscard]] std::string xml_text(std::string_view xml) {
  std::string out;
  bool tag = false;
  char quote = 0;
  for (std::size_t i = 0; i < xml.size(); ++i) {
    const char value = xml[i];
    const bool content = !tag || quote != 0;
    if (content && value == '&') {
      if (const auto entity = entity_at(xml, i)) {
        out += entity->first;
        i = entity->second;
        continue;
      }
    }
    if (tag) {
      if (quote != 0) {
        if (value == quote) {
          quote = 0;
          out += ' ';
        } else {
          out += value;
        }
      } else if (value == '"' || value == '\'') {
        quote = value;
      } else if (value == '>') {
        tag = false;
        out += ' ';
      }
    } else if (value == '<') {
      tag = true;
    } else {
      out += value;
    }
  }
  return out;
}

class Search {
 public:
  Search(std::span<const MatchFinder::Match> needles, std::span<const std::string> placeholders,
         SelfCheck::Result& result)
      : needles_(needles), placeholders_(placeholders), result_(result) {
    for (const MatchFinder::Match& needle : needles) {
      forms_.push_back(forms(needle));
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
    const std::tuple<std::string, std::optional<std::uint32_t>, std::string, std::uint32_t> key{
        needle.search, page, std::string(surface), object};
    if (seen_.insert(key).second) {
      result_.survivors.push_back({needle.search, page, std::string(surface), object});
    }
  }

 private:
  std::span<const MatchFinder::Match> needles_;
  std::span<const std::string> placeholders_;
  std::vector<std::vector<std::string>> forms_;
  SelfCheck::Result& result_;
  std::set<std::tuple<std::string, std::optional<std::uint32_t>, std::string, std::uint32_t>>
      seen_;
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

}  // namespace

SelfCheck::Result SelfCheck::run(platform::ByteSpan pdf, const syntax::Limits& limits,
                                 std::span<const MatchFinder::Match> needles,
                                 std::span<const std::string> placeholders) {
  Result result;
  result.strings = needles.size();
  std::unique_ptr<syntax::Document> document = syntax::Document::open(pdf, limits);
  Search search(needles, placeholders, result);
  if (needles.empty()) {
    return result;
  }
  const std::set<std::string_view> placeholder_fonts{
      writer::PlaceholderFont::metrics(writer::PlaceholderFont::Face::regular).base_font,
      writer::PlaceholderFont::metrics(writer::PlaceholderFont::Face::bold).base_font};

  // Page text and marked-content properties.
  text::TextReader reader(*document);
  result.pages = document->pages().size();
  for (const syntax::Page& source : document->pages()) {
    const text::PageText page = reader.read(source.number);
    MatchFinder finder(page);
    for (const MatchFinder::Match& needle : needles) {
      for (const MatchFinder::Occurrence& found : finder.find(needle, 0)) {
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
    for (const auto& [key, value] : info->entries()) {
      const Object resolved = document->resolve(value);
      if (const Object::String* string = resolved.string()) {
        search.text(text::TextString::to_utf8(string->bytes), std::nullopt, "info", info_number);
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
    for (const std::string_view key :
         {"FontFile", "FontFile2", "FontFile3", "ToUnicode", "CIDSet", "CIDToGIDMap"}) {
      skip(dictionary->get(key));
    }
    if (dictionary->get("Type").is_name("Font") || dictionary->get("Subtype").is_name("Type0")) {
      skip(dictionary->get("Encoding"));
      skip(dictionary->get("UseCMap"));
    }
    if (dictionary->contains("CharProcs")) {
      if (const Dictionary* procedures = document->resolve(dictionary->get("CharProcs")).dictionary()) {
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
    } else if (kind == Kind::scan && (dictionary.get("Subtype").is_name("Form") ||
                                      dictionary.contains("PatternType"))) {
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
      search.raw(std::string(reinterpret_cast<const char*>(decoded.bytes.data()),
                             decoded.bytes.size()),
                 page, number);
      continue;
    }
    const std::vector<std::vector<std::byte>> pieces{decoded.bytes};
    syntax::ContentLexer lexer(pieces, document->limits(), "object " + std::to_string(number));
    std::string operands;
    while (std::optional<syntax::ContentLexer::Operation> operation = lexer.next()) {
      for (const Object& operand : operation->operands) {
        operand_strings(operand, 0, operands);
      }
    }
    search.raw(operands, page, number);
  }
  return result;
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
          "an output in which no redacted string survives in any text surface (page text, "
          "marked-content properties, the Info dictionary, the XMP packet, string objects, "
          "stream bytes)",
          "redact the string where it survives: add a match for page text, set the metadata "
          "fields that hold it, or report the surface as a defect when it is drawn text"};
}

}  // namespace pdfkit::redact
