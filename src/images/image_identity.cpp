// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The canonical serialisation behind an image identity. A value nests at most
// 16 deep; past that it is written as "deep", which no real image needs and
// which keeps a hostile cycle of references from recursing.

#include "images/image_identity.hpp"

#include "platform/crypto/sha2.hpp"
#include "syntax/filters.hpp"
#include "syntax/object.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

namespace pdfkit::images {

namespace {

using platform::crypto::Sha2;
using syntax::Dictionary;
using syntax::Document;
using syntax::Object;

constexpr int max_depth = 16;

// The decisive entries of CLAUDE.md section 4, sorted.
constexpr std::array<std::string_view, 10> decisive_keys{
    "BitsPerComponent", "ColorSpace", "Decode", "DecodeParms", "Filter",
    "Height",           "ImageMask",  "Mask",   "SMask",       "Width"};

std::string hex(const std::vector<std::byte>& bytes, std::size_t count) {
  static constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < count && i < bytes.size(); ++i) {
    const auto value = std::to_integer<unsigned>(bytes[i]);
    out.push_back(digits[value >> 4U]);
    out.push_back(digits[value & 15U]);
  }
  return out;
}

std::string full_name(std::string_view name) {
  if (name == "G") {
    return "DeviceGray";
  }
  if (name == "RGB") {
    return "DeviceRGB";
  }
  if (name == "CMYK") {
    return "DeviceCMYK";
  }
  if (name == "I") {
    return "Indexed";
  }
  return std::string(syntax::Filters::full_name(name));
}

void number(std::string& out, const Object::Number& value) {
  std::array<char, 64> buffer{};
  if (value.is_integer) {
    const auto result = std::to_chars(buffer.begin(), buffer.end(), value.integer);
    out.append(buffer.data(), result.ptr);
    return;
  }
  const double v = value.value;
  if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 9.0e15) {
    const auto result = std::to_chars(buffer.begin(), buffer.end(), static_cast<std::int64_t>(v));
    out.append(buffer.data(), result.ptr);
    return;
  }
  const auto result = std::to_chars(buffer.begin(), buffer.end(), v);
  out.append(buffer.data(), result.ptr);
}

void canonical(Document& document, const Object& object, std::string& out, int depth);

void canonical_dictionary(Document& document, const Dictionary& dictionary, std::string& out,
                          int depth, bool skip_length) {
  std::vector<std::pair<std::string, const Object*>> entries;
  for (const auto& [key, value] : dictionary.entries()) {
    const std::string full = ImageIdentity::full_key(key);
    if (skip_length && full == "Length") {
      continue;
    }
    entries.emplace_back(full, &value);
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  out += "<<";
  for (const auto& [key, value] : entries) {
    out += "/" + key + " ";
    canonical(document, *value, out, depth + 1);
    out += " ";
  }
  out += ">>";
}

void canonical(Document& document, const Object& object, std::string& out, int depth) {
  if (depth > max_depth) {
    out += "deep";
    return;
  }
  const Object value = document.resolve(object);
  switch (value.kind()) {
    case Object::Kind::null:
    case Object::Kind::reference:
      out += "null";
      return;
    case Object::Kind::boolean:
      out += *value.boolean() ? "true" : "false";
      return;
    case Object::Kind::number:
      number(out, *value.number_value());
      return;
    case Object::Kind::string: {
      const std::string& bytes = value.string()->bytes;
      std::vector<std::byte> raw(bytes.size());
      std::transform(bytes.begin(), bytes.end(), raw.begin(),
                     [](char c) { return static_cast<std::byte>(c); });
      out += "<" + hex(raw, raw.size()) + ">";
      return;
    }
    case Object::Kind::name:
      out += "/" + full_name(*value.name());
      return;
    case Object::Kind::array:
      out += "[";
      for (const Object& item : *value.array()) {
        canonical(document, item, out, depth + 1);
        out += " ";
      }
      out += "]";
      return;
    case Object::Kind::dictionary:
      canonical_dictionary(document, *value.dictionary(), out, depth, false);
      return;
    case Object::Kind::stream: {
      const syntax::Stream& stream = *value.stream();
      const std::vector<std::byte> bytes = ImageIdentity::encoded_bytes(document, stream);
      out += "stream(" + hex(Sha2::digest(Sha2::Variant::sha256, bytes), 32) + " ";
      canonical_dictionary(document, stream.dictionary(), out, depth, true);
      out += ")";
      return;
    }
  }
}

std::string identity(Document& document, const Dictionary& dictionary, platform::ByteSpan data,
                     int depth) {
  std::string text;
  // An entry is found under its full or its abbreviated key.
  for (const std::string_view key : decisive_keys) {
    const Object* value = nullptr;
    for (const auto& [written, entry] : dictionary.entries()) {
      if (ImageIdentity::full_key(written) == key) {
        value = &entry;
      }
    }
    if (value == nullptr) {
      continue;
    }
    text += "/" + std::string(key) + " ";
    const Object resolved = document.resolve(*value);
    const syntax::Stream* mask = resolved.stream();
    if ((key == "SMask" || key == "Mask") && mask != nullptr && depth < 4) {
      const std::vector<std::byte> bytes = ImageIdentity::encoded_bytes(document, *mask);
      text += identity(document, mask->dictionary(), bytes, depth + 1);
    } else {
      canonical(document, resolved, text, 1);
    }
    text += "\n";
  }
  Sha2 hash(Sha2::Variant::sha256);
  hash.update(platform::as_bytes(text));
  hash.update(platform::as_bytes("\n"));
  hash.update(data);
  return "img:" + hex(hash.finish(), 8);
}

}  // namespace

std::string ImageIdentity::full_key(const std::string& key) {
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 10> keys{{
      {"BPC", "BitsPerComponent"},
      {"CS", "ColorSpace"},
      {"D", "Decode"},
      {"DP", "DecodeParms"},
      {"F", "Filter"},
      {"H", "Height"},
      {"IM", "ImageMask"},
      {"I", "Interpolate"},
      {"W", "Width"},
      {"L", "Length"},
  }};
  for (const auto& [abbreviation, full] : keys) {
    if (key == abbreviation) {
      return std::string(full);
    }
  }
  return key;
}

std::vector<std::byte> ImageIdentity::encoded_bytes(Document& document,
                                                    const syntax::Stream& stream) {
  const platform::ByteSpan raw = document.raw(stream);
  if (document.security()) {
    return document.security()->decrypt_stream(stream.owner(), stream.dictionary(), raw);
  }
  return {raw.begin(), raw.end()};
}

std::string ImageIdentity::of_stream(Document& document, const syntax::Stream& stream) {
  const std::vector<std::byte> bytes = encoded_bytes(document, stream);
  return identity(document, stream.dictionary(), bytes, 0);
}

std::string ImageIdentity::of_inline(Document& document, const Dictionary& dictionary,
                                     platform::ByteSpan data) {
  return identity(document, dictionary, data, 0);
}

}  // namespace pdfkit::images
