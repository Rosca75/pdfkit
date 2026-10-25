// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The property rewrite. A value is replaced needle by needle, each pass over
// the result of the one before, so two strings that overlap are both gone.
// The inline BDC is written with the writer's object serialiser, which never
// meets a reference here because a content stream's operands hold none.

#include "redact/marked_content_rewriter.hpp"

#include "platform/unicode/folding.hpp"
#include "text/text_string.hpp"
#include "writer/serialiser.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace pdfkit::redact {

namespace {

using TextFolding = platform::unicode::Folding;

[[nodiscard]] bool ascii(std::string_view text) noexcept {
  for (const char value : text) {
    const auto byte = static_cast<unsigned char>(value);
    if (byte < 0x20 || byte >= 0x7F) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::size_t MarkedContentRewriter::closing(const text::ContentStream& stream, std::size_t open) {
  int depth = 0;
  for (std::size_t i = open; i < stream.operations.size(); ++i) {
    const std::string& op = stream.operations[i].op;
    if (op == "BMC" || op == "BDC") {
      ++depth;
    } else if (op == "EMC") {
      if (--depth == 0) {
        return i;
      }
    }
  }
  return stream.operations.size();
}

std::optional<std::string> MarkedContentRewriter::replace(
    std::string_view value, std::span<const MatchFinder::Match> needles) {
  std::u32string text = TextFolding::decode(value);
  bool changed = false;
  for (const MatchFinder::Match& needle : needles) {
    const auto ranges = MatchFinder::ranges_in(text, needle);
    if (ranges.empty()) {
      continue;
    }
    changed = true;
    const std::u32string placeholder = TextFolding::decode(needle.placeholder);
    std::u32string out;
    std::size_t cursor = 0;
    for (const auto& [first, last] : ranges) {
      out.append(text, cursor, first - cursor);
      out += placeholder;
      cursor = last;
    }
    out.append(text, cursor);
    text = std::move(out);
  }
  if (!changed) {
    return std::nullopt;
  }
  return TextFolding::encode(text);
}

std::optional<syntax::Dictionary> MarkedContentRewriter::rewrite(
    const syntax::Dictionary& properties, std::span<const MatchFinder::Match> needles,
    const std::string* whole, const std::function<syntax::Object(const syntax::Object&)>& resolve) {
  syntax::Dictionary out = properties;
  bool changed = false;
  for (const std::string_view key : keys) {
    const syntax::Object* entry = properties.find(key);
    if (entry == nullptr) {
      continue;
    }
    const syntax::Object value = resolve(*entry);
    const syntax::Object::String* string = value.string();
    if (string == nullptr) {
      continue;
    }
    const std::string utf8 = text::TextString::to_utf8(string->bytes);
    std::optional<std::string> replaced;
    if (whole != nullptr && key == "ActualText") {
      // The span draws nothing now; its replacement text goes with its
      // glyphs, so the placeholder drawn beside it is read once.
      out.erase(key);
      changed = true;
      continue;
    }
    if (whole != nullptr) {
      replaced = *whole;
    } else {
      replaced = replace(utf8, needles);
    }
    if (replaced && (*replaced != utf8 || entry->reference())) {
      out.set(std::string(key), text_string(*replaced));
      changed = true;
    }
  }
  // Every other string of the list (an artifact's /Contents, a producer's
  // own key) is a place an extractor can copy the string from too.
  for (const auto& [key, value] : properties.entries()) {
    if (std::ranges::find(keys, std::string_view(key)) != keys.end()) {
      continue;
    }
    bool any = false;
    syntax::Object replaced = replace_strings(value, needles, 0, any);
    if (any) {
      out.set(key, std::move(replaced));
      changed = true;
    }
  }
  if (!changed) {
    return std::nullopt;
  }
  return out;
}

syntax::Object MarkedContentRewriter::replace_strings(const syntax::Object& value,
                                                      std::span<const MatchFinder::Match> needles,
                                                      int depth, bool& changed) {
  constexpr int max_depth = 32;
  if (depth > max_depth) {
    return value;
  }
  if (const syntax::Object::String* string = value.string()) {
    if (const auto replaced = replace(text::TextString::to_utf8(string->bytes), needles)) {
      changed = true;
      return text_string(*replaced);
    }
    return value;
  }
  if (const syntax::Object::Array* items = value.array()) {
    syntax::Object::Array out;
    bool any = false;
    for (const syntax::Object& item : *items) {
      out.push_back(replace_strings(item, needles, depth + 1, any));
    }
    changed = changed || any;
    return any ? syntax::Object::make_array(std::move(out)) : value;
  }
  if (const syntax::Dictionary* dictionary = value.dictionary()) {
    syntax::Dictionary out = *dictionary;
    bool any = false;
    for (const auto& [key, item] : dictionary->entries()) {
      bool here = false;
      syntax::Object replaced = replace_strings(item, needles, depth + 1, here);
      if (here) {
        out.set(key, std::move(replaced));
        any = true;
      }
    }
    changed = changed || any;
    return any ? syntax::Object::make_dictionary(std::move(out)) : value;
  }
  return value;
}

std::string MarkedContentRewriter::operation(const syntax::Object& tag,
                                             const syntax::Dictionary& properties,
                                             std::string_view op) {
  const writer::Serialiser::Numbering same = [](syntax::Object::Reference reference) {
    return std::optional<std::uint32_t>(reference.number);
  };
  std::string out;
  writer::Serialiser::write(out, tag, same);
  out += ' ';
  writer::Serialiser::write(out, syntax::Object::make_dictionary(properties), same);
  out += ' ';
  out += op;
  return out;
}

syntax::Object MarkedContentRewriter::text_string(std::string_view utf8) {
  if (ascii(utf8)) {
    return syntax::Object::make_string(std::string(utf8));
  }
  std::string bytes = "\xFE\xFF";
  for (const char32_t value : TextFolding::decode(utf8)) {
    const auto unit = [&bytes](char32_t code) {
      bytes.push_back(static_cast<char>((code >> 8U) & 0xFFU));
      bytes.push_back(static_cast<char>(code & 0xFFU));
    };
    if (value >= 0x10000 && value <= 0x10FFFF) {
      const char32_t offset = value - 0x10000;
      unit(0xD800 + (offset >> 10U));
      unit(0xDC00 + (offset & 0x3FFU));
    } else {
      unit(value);
    }
  }
  return syntax::Object::make_string(std::move(bytes), true);
}

}  // namespace pdfkit::redact
