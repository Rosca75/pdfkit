// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The parser walks the tokens once. Each begin...end block reads its entries
// as tuples of tokens; an entry whose tokens are not of the expected kinds is
// counted as malformed and the walk resumes at the next token, so a damaged
// entry costs that entry alone. A block also ends at a keyword that opens
// another block, as a truncated CMap leaves no end keyword.

#include "fonts/cmap_parser.hpp"

#include "fonts/glyph_list.hpp"
#include "platform/utf8_sequence.hpp"
#include "syntax/lexer.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace pdfkit::fonts {

namespace {

using syntax::Lexer;
using TokenKind = Lexer::TokenKind;

// The value of a code string of 1 to 4 bytes.
std::optional<std::uint32_t> code_of(const std::string& bytes) noexcept {
  if (bytes.empty() || bytes.size() > 4) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  for (const char c : bytes) {
    value = (value << 8U) | static_cast<unsigned char>(c);
  }
  return value;
}

bool is_string(const Lexer::Token& token) noexcept {
  return token.kind == TokenKind::hex_string || token.kind == TokenKind::literal_string;
}

bool ends_block(const Lexer::Token& token) noexcept {
  if (token.kind == TokenKind::end) {
    return true;
  }
  if (token.kind != TokenKind::keyword) {
    return false;
  }
  const std::string_view word = token.text;
  return word.starts_with("end") || word.starts_with("begin");
}

class Walk {
 public:
  explicit Walk(platform::ByteSpan bytes) : lexer_(bytes) {}

  Cmap run() {
    std::optional<Lexer::Token> previous;
    while (true) {
      Lexer::Token token = lexer_.next();
      if (token.kind == TokenKind::end) {
        break;
      }
      if (token.kind == TokenKind::keyword) {
        if (token.text == "begincodespacerange") {
          codespace();
        } else if (token.text == "begincidrange") {
          ranges(false);
        } else if (token.text == "beginnotdefrange") {
          ranges(true);
        } else if (token.text == "begincidchar") {
          cid_chars();
        } else if (token.text == "beginbfrange") {
          bf_ranges();
        } else if (token.text == "beginbfchar") {
          bf_chars();
        } else if (token.text == "usecmap") {
          use(previous);
        } else if (token.text == "def" && wmode_pending_) {
          wmode_pending_ = false;
        }
      } else if (token.kind == TokenKind::name && token.text == "WMode") {
        Lexer::Token value = lexer_.next();
        if (value.kind == TokenKind::integer) {
          map_.set_vertical(value.integer == 1);
        }
        token = std::move(value);
      }
      previous = std::move(token);
    }
    return std::move(map_);
  }

 private:
  // Reads the next token unless it ends the block; no value at the block's end.
  std::optional<Lexer::Token> entry_token() {
    const std::size_t at = lexer_.position();
    Lexer::Token token = lexer_.next();
    if (ends_block(token)) {
      if (token.kind == TokenKind::keyword && token.text.starts_with("begin")) {
        lexer_.seek(at);
      }
      return std::nullopt;
    }
    return token;
  }

  void codespace() {
    while (true) {
      std::optional<Lexer::Token> low = entry_token();
      if (!low) {
        return;
      }
      std::optional<Lexer::Token> high = entry_token();
      if (!high) {
        map_.count_malformed();
        return;
      }
      const std::optional<std::uint32_t> a = is_string(*low) ? code_of(low->text) : std::nullopt;
      const std::optional<std::uint32_t> b = is_string(*high) ? code_of(high->text) : std::nullopt;
      if (!a || !b || low->text.size() != high->text.size()) {
        map_.count_malformed();
        continue;
      }
      map_.add_codespace(*a, *b, low->text.size());
    }
  }

  // begincidrange and beginnotdefrange: <low> <high> cid. A notdef range
  // names the glyph drawn for codes without a CID; it decodes nothing, so
  // it is read and set aside.
  void ranges(bool notdef) {
    while (true) {
      std::optional<Lexer::Token> low = entry_token();
      if (!low) {
        return;
      }
      std::optional<Lexer::Token> high = entry_token();
      std::optional<Lexer::Token> cid = high ? entry_token() : std::nullopt;
      if (!high || !cid) {
        map_.count_malformed();
        return;
      }
      const std::optional<std::uint32_t> a = is_string(*low) ? code_of(low->text) : std::nullopt;
      const std::optional<std::uint32_t> b = is_string(*high) ? code_of(high->text) : std::nullopt;
      if (!a || !b || cid->kind != TokenKind::integer || cid->integer < 0 ||
          cid->integer > 0xFFFFFFFFLL) {
        map_.count_malformed();
        continue;
      }
      if (!notdef) {
        map_.add_cid_range(*a, *b, static_cast<std::uint32_t>(cid->integer));
      }
    }
  }

  void cid_chars() {
    while (true) {
      std::optional<Lexer::Token> code = entry_token();
      if (!code) {
        return;
      }
      std::optional<Lexer::Token> cid = entry_token();
      if (!cid) {
        map_.count_malformed();
        return;
      }
      const std::optional<std::uint32_t> a = is_string(*code) ? code_of(code->text) : std::nullopt;
      if (!a || cid->kind != TokenKind::integer || cid->integer < 0 ||
          cid->integer > 0xFFFFFFFFLL) {
        map_.count_malformed();
        continue;
      }
      map_.add_cid_range(*a, *a, static_cast<std::uint32_t>(cid->integer));
    }
  }

  // A destination written as a string, or as a glyph name; no value for any
  // other token.
  static std::optional<std::string> destination(const Lexer::Token& token) {
    if (is_string(token)) {
      return token.text;
    }
    if (token.kind == TokenKind::name) {
      if (const std::optional<std::string> text = GlyphList::to_unicode(token.text)) {
        return CmapParser::utf8_to_utf16be(*text);
      }
    }
    return std::nullopt;
  }

  void bf_ranges() {
    while (true) {
      std::optional<Lexer::Token> low = entry_token();
      if (!low) {
        return;
      }
      std::optional<Lexer::Token> high = entry_token();
      std::optional<Lexer::Token> target = high ? entry_token() : std::nullopt;
      if (!high || !target) {
        map_.count_malformed();
        return;
      }
      const std::optional<std::uint32_t> a = is_string(*low) ? code_of(low->text) : std::nullopt;
      const std::optional<std::uint32_t> b = is_string(*high) ? code_of(high->text) : std::nullopt;
      if (target->kind == TokenKind::array_open) {
        std::vector<std::string> items;
        bool closed = false;
        while (std::optional<Lexer::Token> item = entry_token()) {
          if (item->kind == TokenKind::array_close) {
            closed = true;
            break;
          }
          std::optional<std::string> text = destination(*item);
          items.push_back(text ? std::move(*text) : std::string());
        }
        if (!a || !b || *a > *b) {
          map_.count_malformed();
        } else {
          for (std::size_t i = 0; i < items.size() && *a + i <= *b; ++i) {
            if (!items[i].empty()) {
              map_.add_unicode(static_cast<std::uint32_t>(*a + i), items[i]);
            }
          }
        }
        if (!closed) {
          return;
        }
        continue;
      }
      const std::optional<std::string> start = destination(*target);
      if (!a || !b || !start || *a > *b) {
        map_.count_malformed();
        continue;
      }
      map_.add_unicode_range(*a, *b, *start);
    }
  }

  void bf_chars() {
    while (true) {
      std::optional<Lexer::Token> code = entry_token();
      if (!code) {
        return;
      }
      std::optional<Lexer::Token> target = entry_token();
      if (!target) {
        map_.count_malformed();
        return;
      }
      const std::optional<std::uint32_t> a = is_string(*code) ? code_of(code->text) : std::nullopt;
      const std::optional<std::string> text = destination(*target);
      if (!a || !text) {
        map_.count_malformed();
        continue;
      }
      map_.add_unicode(*a, *text);
    }
  }

  void use(const std::optional<Lexer::Token>& previous) {
    if (previous && previous->kind == TokenKind::name &&
        (previous->text == "Identity-H" || previous->text == "Identity-V")) {
      map_.inherit(Cmap::identity(previous->text == "Identity-V"));
      return;
    }
    map_.count_malformed();
  }

  Lexer lexer_;
  Cmap map_;
  bool wmode_pending_ = false;
};

}  // namespace

Cmap CmapParser::parse(platform::ByteSpan bytes) {
  return Walk(bytes).run();
}

std::string CmapParser::utf8_to_utf16be(const std::string& text) {
  std::string out;
  std::size_t at = 0;
  const auto unit = [&out](std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<char>(value & 0xFFU));
  };
  while (at < text.size()) {
    const platform::Utf8Sequence sequence = platform::next_utf8_sequence(text, at);
    at += sequence.length;
    if (!sequence.valid) {
      continue;
    }
    const auto value = static_cast<std::uint32_t>(sequence.code_point);
    if (value >= 0x10000) {
      const std::uint32_t offset = value - 0x10000;
      unit(0xD800 + (offset >> 10U));
      unit(0xDC00 + (offset & 0x3FFU));
    } else {
      unit(value);
    }
  }
  return out;
}

}  // namespace pdfkit::fonts
