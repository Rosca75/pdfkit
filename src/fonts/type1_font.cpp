// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The clear text is read with the PDF lexer, which PostScript's token syntax
// shares. The private part is scanned as bytes, because charstrings are binary
// data introduced by a length and an RD (or "-|") token followed by one space.
// pdf.js src/core/type1_parser.js (Apache-2.0, read) reads the same way and
// is the reference for the hexadecimal eexec test (four hexadecimal digits
// first) and the RD token spellings.

#include "fonts/type1_font.hpp"

#include "fonts/encodings.hpp"
#include "syntax/lexer.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace pdfkit::fonts {

namespace {

using platform::ByteSpan;

constexpr std::uint16_t eexec_key = 55665;
constexpr std::uint16_t charstring_key = 4330;
constexpr std::uint16_t c1 = 52845;
constexpr std::uint16_t c2 = 22719;

bool is_space(std::uint8_t value) noexcept {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' ||
         value == 0;
}

bool is_hex(std::uint8_t value) noexcept {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
}

unsigned hex_digit(std::uint8_t value) noexcept {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10U;
  }
  return value - 'A' + 10U;
}

// The bytes with PFB segment headers removed: 0x80, a type (1 text, 2
// binary, 3 end) and a four-byte little-endian length before each segment.
std::vector<std::uint8_t> unwrap(ByteSpan data) {
  std::vector<std::uint8_t> out;
  out.reserve(data.size());
  if (data.empty() || std::to_integer<std::uint8_t>(data[0]) != 0x80) {
    for (const std::byte value : data) {
      out.push_back(std::to_integer<std::uint8_t>(value));
    }
    return out;
  }
  std::size_t at = 0;
  while (at + 6 <= data.size() && std::to_integer<std::uint8_t>(data[at]) == 0x80) {
    const auto type = std::to_integer<std::uint8_t>(data[at + 1]);
    if (type == 3) {
      break;
    }
    const std::uint32_t length = platform::read_le<std::uint32_t>(data, at + 2).value_or(0);
    at += 6;
    const std::size_t take = std::min<std::size_t>(length, data.size() - at);
    for (std::size_t i = 0; i < take; ++i) {
      out.push_back(std::to_integer<std::uint8_t>(data[at + i]));
    }
    at += take;
  }
  return out;
}

std::size_t find(const std::vector<std::uint8_t>& data, std::string_view needle,
                 std::size_t from = 0) {
  if (needle.empty() || data.size() < needle.size()) {
    return std::string_view::npos;
  }
  const auto found =
      std::search(data.begin() + static_cast<std::ptrdiff_t>(std::min(from, data.size())),
                  data.end(), needle.begin(), needle.end(),
                  [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); });
  if (found == data.end()) {
    return std::string_view::npos;
  }
  return static_cast<std::size_t>(found - data.begin());
}

// A Type 1 charstring's width: the wx of its first command when that is
// hsbw (13) or sbw (12 7).
std::optional<double> charstring_width(const std::vector<std::uint8_t>& code) {
  std::vector<double> stack;
  std::size_t at = 0;
  while (at < code.size()) {
    const std::uint8_t v = code[at];
    if (v >= 32 && v <= 246) {
      stack.push_back(static_cast<double>(v) - 139.0);
      at += 1;
    } else if (v >= 247 && v <= 250 && at + 1 < code.size()) {
      stack.push_back((v - 247) * 256.0 + code[at + 1] + 108.0);
      at += 2;
    } else if (v >= 251 && v <= 254 && at + 1 < code.size()) {
      stack.push_back(-(v - 251) * 256.0 - code[at + 1] - 108.0);
      at += 2;
    } else if (v == 255 && at + 4 < code.size()) {
      const std::uint32_t raw = (std::uint32_t{code[at + 1]} << 24U) |
                                (std::uint32_t{code[at + 2]} << 16U) |
                                (std::uint32_t{code[at + 3]} << 8U) | code[at + 4];
      stack.push_back(static_cast<double>(static_cast<std::int32_t>(raw)));
      at += 5;
    } else if (v == 13) {
      return stack.size() >= 2 ? std::optional<double>(stack[1]) : std::nullopt;
    } else if (v == 12 && at + 1 < code.size() && code[at + 1] == 7) {
      return stack.size() >= 3 ? std::optional<double>(stack[2]) : std::nullopt;
    } else {
      return std::nullopt;
    }
    if (stack.size() > 24) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

}  // namespace

std::vector<std::uint8_t> Type1Font::decrypt(const std::vector<std::uint8_t>& cipher,
                                             std::uint16_t key) {
  std::vector<std::uint8_t> plain;
  plain.reserve(cipher.size());
  std::uint32_t r = key;
  for (const std::uint8_t value : cipher) {
    plain.push_back(static_cast<std::uint8_t>(value ^ (r >> 8U)));
    r = ((value + r) * c1 + c2) & 0xFFFFU;
  }
  return plain;
}

std::vector<std::uint8_t> Type1Font::encrypt(const std::vector<std::uint8_t>& plain,
                                             std::uint16_t key) {
  std::vector<std::uint8_t> cipher;
  cipher.reserve(plain.size());
  std::uint32_t r = key;
  for (const std::uint8_t value : plain) {
    const auto out = static_cast<std::uint8_t>(value ^ (r >> 8U));
    cipher.push_back(out);
    r = ((out + r) * c1 + c2) & 0xFFFFU;
  }
  return cipher;
}

std::optional<Type1Font> Type1Font::parse(ByteSpan data, platform::Budget& budget) {
  budget.check_claim(data.size(), "a Type 1 font program");
  const std::vector<std::uint8_t> bytes = unwrap(data);
  const std::size_t eexec = find(bytes, "eexec");
  const bool header = find(bytes, "%!PS-AdobeFont") == 0 || find(bytes, "%!FontType1") == 0 ||
                      find(bytes, "%!PS") == 0;
  if (!header && eexec == std::string_view::npos) {
    return std::nullopt;
  }
  Type1Font font;
  const std::size_t clear_end = eexec == std::string_view::npos ? bytes.size() : eexec;
  font.read_clear_text(std::string_view(reinterpret_cast<const char*>(bytes.data()), clear_end));
  if (eexec == std::string_view::npos) {
    return font;
  }
  std::size_t at = eexec + 5;
  while (at < bytes.size() && is_space(bytes[at])) {
    ++at;
  }
  std::vector<std::uint8_t> cipher;
  const bool hex = at + 4 <= bytes.size() &&
                   std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                               bytes.begin() + static_cast<std::ptrdiff_t>(at + 4), is_hex);
  if (hex) {
    cipher.reserve((bytes.size() - at) / 2);
    int pending = -1;
    for (; at < bytes.size(); ++at) {
      const std::uint8_t value = bytes[at];
      if (is_space(value)) {
        continue;
      }
      if (!is_hex(value)) {
        break;
      }
      if (pending < 0) {
        pending = static_cast<int>(hex_digit(value));
      } else {
        cipher.push_back(
            static_cast<std::uint8_t>((static_cast<unsigned>(pending) << 4U) | hex_digit(value)));
        pending = -1;
      }
    }
  } else {
    cipher.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end());
  }
  std::vector<std::uint8_t> plain = decrypt(cipher, eexec_key);
  if (plain.size() < 4) {
    return font;
  }
  plain.erase(plain.begin(), plain.begin() + 4);
  font.read_private(plain, budget);
  return font;
}

void Type1Font::read_clear_text(std::string_view text) {
  syntax::Lexer lexer(platform::as_bytes(text));
  using Kind = syntax::Lexer::TokenKind;
  while (true) {
    const syntax::Lexer::Token token = lexer.next();
    if (token.kind == Kind::end) {
      return;
    }
    if (token.kind != Kind::name) {
      continue;
    }
    if (token.text == "FontMatrix") {
      std::vector<double> values;
      for (syntax::Lexer::Token item = lexer.next();
           item.kind != Kind::end && item.kind != Kind::array_close &&
           item.kind != Kind::brace_close && values.size() < 6;
           item = lexer.next()) {
        if (item.is_number()) {
          values.push_back(item.number());
        }
      }
      if (values.size() == 6 && std::isfinite(values[0]) && values[0] != 0.0) {
        scale_ = values[0];
      }
    } else if (token.text == "FontBBox") {
      std::vector<double> values;
      for (syntax::Lexer::Token item = lexer.next();
           item.kind != Kind::end && item.kind != Kind::array_close &&
           item.kind != Kind::brace_close && values.size() < 4;
           item = lexer.next()) {
        if (item.is_number()) {
          values.push_back(item.number());
        }
      }
      for (std::size_t i = 0; i < values.size() && i < 4; ++i) {
        bbox_[i] = values[i];
      }
    } else if (token.text == "Encoding") {
      const syntax::Lexer::Token value = lexer.next();
      if (value.is_keyword("StandardEncoding")) {
        standard_encoding_ = true;
        continue;
      }
      if (value.kind != Kind::integer) {
        continue;
      }
      standard_encoding_ = false;
      // "dup <code> /<name> put" entries up to the closing "def".
      std::optional<std::int64_t> code;
      for (syntax::Lexer::Token item = lexer.next(); item.kind != Kind::end; item = lexer.next()) {
        if (item.is_keyword("def") || item.is_keyword("readonly")) {
          break;
        }
        if (item.kind == Kind::integer) {
          code = item.integer;
        } else if (item.kind == Kind::name && code && *code >= 0 && *code < 256) {
          encoding_[static_cast<std::size_t>(*code)] = item.text;
          code.reset();
        } else if (item.is_keyword("put")) {
          code.reset();
        }
      }
    }
  }
}

void Type1Font::read_private(const std::vector<std::uint8_t>& plain, platform::Budget& budget) {
  int len_iv = 4;
  if (const std::size_t at = find(plain, "/lenIV"); at != std::string_view::npos) {
    std::size_t i = at + 6;
    while (i < plain.size() && is_space(plain[i])) {
      ++i;
    }
    bool negative = false;
    if (i < plain.size() && plain[i] == '-') {
      negative = true;
      ++i;
    }
    int value = 0;
    for (; i < plain.size() && plain[i] >= '0' && plain[i] <= '9' && value < 1000; ++i) {
      value = value * 10 + (plain[i] - '0');
    }
    len_iv = negative ? -value : value;
  }
  std::size_t at = find(plain, "/CharStrings");
  if (at == std::string_view::npos) {
    return;
  }
  at += 12;
  // Skips "<count> dict dup begin" up to the first glyph name.
  while (at < plain.size() && plain[at] != '/') {
    if (plain[at] == 'e' && find(plain, "end", at) == at) {
      return;
    }
    ++at;
  }
  while (at < plain.size()) {
    while (at < plain.size() && is_space(plain[at])) {
      ++at;
    }
    if (at >= plain.size() || plain[at] != '/') {
      return;
    }
    ++at;
    std::string name;
    while (at < plain.size() && !is_space(plain[at]) && plain[at] != '/' && name.size() < 256) {
      name.push_back(static_cast<char>(plain[at++]));
    }
    while (at < plain.size() && is_space(plain[at])) {
      ++at;
    }
    std::size_t length = 0;
    bool digits = false;
    for (; at < plain.size() && plain[at] >= '0' && plain[at] <= '9' && length < 1000000; ++at) {
      length = length * 10 + (plain[at] - '0');
      digits = true;
    }
    if (!digits) {
      return;
    }
    while (at < plain.size() && is_space(plain[at])) {
      ++at;
    }
    // The RD token: "RD" or "-|" (any non-space run), then exactly one space.
    while (at < plain.size() && !is_space(plain[at])) {
      ++at;
    }
    ++at;
    if (at > plain.size() || length > plain.size() - at) {
      return;
    }
    budget.check_claim(length, "a Type 1 charstring");
    std::vector<std::uint8_t> code(plain.begin() + static_cast<std::ptrdiff_t>(at),
                                   plain.begin() + static_cast<std::ptrdiff_t>(at + length));
    at += length;
    if (len_iv >= 0) {
      code = decrypt(code, charstring_key);
      const std::size_t skip = std::min<std::size_t>(static_cast<std::size_t>(len_iv), code.size());
      code.erase(code.begin(), code.begin() + static_cast<std::ptrdiff_t>(skip));
    }
    widths_.emplace(std::move(name), charstring_width(code));
    // Skips "ND" or "|-" up to the next glyph name or the end of the dict.
    while (at < plain.size() && plain[at] != '/') {
      if (plain[at] == 'e' && find(plain, "end", at) == at) {
        return;
      }
      ++at;
    }
  }
}

std::string_view Type1Font::encoding_name(std::uint8_t code) const noexcept {
  if (standard_encoding_) {
    return Encodings::name(Encodings::Base::standard, code);
  }
  return encoding_[code];
}

bool Type1Font::has_glyph(std::string_view name) const {
  return widths_.contains(name);
}

std::optional<double> Type1Font::width(std::string_view name) const {
  const auto found = widths_.find(name);
  if (found == widths_.end()) {
    return std::nullopt;
  }
  const std::optional<double>& wx = found->second;
  if (!wx) {
    return std::nullopt;
  }
  return *wx * scale_ * 1000.0;
}

}  // namespace pdfkit::fonts
