// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// A recursive-descent reader of the RFC 8259 grammar (recalled), bounded to 64
// levels of nesting. String escapes are decoded, including surrogate pairs;
// numbers are kept as their raw text, which is all the CLI needs of them.

#include "json_text.hpp"

#include <cstdint>

namespace pdfkit::cli {

namespace {

constexpr std::size_t max_depth = 64;

class Reader {
 public:
  Reader(std::string_view text, std::vector<JsonText::Node>& nodes) : text_(text), nodes_(nodes) {}

  bool parse_document() {
    skip_space();
    if (!parse_value(0)) {
      return false;
    }
    skip_space();
    return pos_ == text_.size();
  }

  [[nodiscard]] std::size_t offset() const { return pos_; }

 private:
  bool at_end() const { return pos_ >= text_.size(); }

  void skip_space() {
    while (!at_end() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
                         text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool literal(std::string_view word) {
    if (text_.substr(pos_, word.size()) != word) {
      return false;
    }
    pos_ += word.size();
    return true;
  }

  bool parse_value(std::size_t depth) {
    if (at_end() || depth > max_depth) {
      return false;
    }
    const std::size_t index = nodes_.size();
    nodes_.emplace_back();
    const std::size_t start = pos_;
    bool ok = false;
    const char c = text_[pos_];
    if (c == '{') {
      ok = parse_object(index, depth);
    } else if (c == '[') {
      ok = parse_array(index, depth);
    } else if (c == '"') {
      std::string decoded;
      ok = parse_string(decoded);
      nodes_[index].kind = JsonText::Kind::string;
      nodes_[index].text = std::move(decoded);
    } else if (c == 't' || c == 'f') {
      ok = literal(c == 't' ? "true" : "false");
      nodes_[index].kind = JsonText::Kind::boolean;
      nodes_[index].text = c == 't' ? "true" : "false";
    } else if (c == 'n') {
      ok = literal("null");
    } else if (c == '-' || (c >= '0' && c <= '9')) {
      ok = parse_number();
      nodes_[index].kind = JsonText::Kind::number;
    }
    nodes_[index].raw = text_.substr(start, pos_ - start);
    return ok;
  }

  bool parse_object(std::size_t index, std::size_t depth) {
    nodes_[index].kind = JsonText::Kind::object;
    ++pos_;
    skip_space();
    if (!at_end() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      skip_space();
      std::string key;
      if (at_end() || text_[pos_] != '"' || !parse_string(key)) {
        return false;
      }
      skip_space();
      if (at_end() || text_[pos_] != ':') {
        return false;
      }
      ++pos_;
      skip_space();
      const std::size_t child = nodes_.size();
      if (!parse_value(depth + 1)) {
        return false;
      }
      nodes_[index].members.emplace_back(std::move(key), child);
      skip_space();
      if (at_end()) {
        return false;
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] != '}') {
        return false;
      }
      ++pos_;
      return true;
    }
  }

  bool parse_array(std::size_t index, std::size_t depth) {
    nodes_[index].kind = JsonText::Kind::array;
    ++pos_;
    skip_space();
    if (!at_end() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      skip_space();
      const std::size_t child = nodes_.size();
      if (!parse_value(depth + 1)) {
        return false;
      }
      nodes_[index].items.push_back(child);
      skip_space();
      if (at_end()) {
        return false;
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] != ']') {
        return false;
      }
      ++pos_;
      return true;
    }
  }

  bool parse_number() {
    if (text_[pos_] == '-') {
      ++pos_;
    }
    const std::size_t digits = pos_;
    while (!at_end() &&
           ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.' ||
            text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-')) {
      ++pos_;
    }
    return pos_ > digits;
  }

  bool hex4(std::uint32_t& value) {
    value = 0;
    for (int i = 0; i < 4; ++i) {
      if (at_end()) {
        return false;
      }
      const char c = text_[pos_++];
      std::uint32_t digit = 0;
      if (c >= '0' && c <= '9') {
        digit = static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        digit = static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        digit = static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
      value = (value << 4U) | digit;
    }
    return true;
  }

  static void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80U) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800U) {
      out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
      out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000U) {
      out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
      out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
      out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
  }

  bool parse_string(std::string& out) {
    ++pos_;
    while (!at_end()) {
      const char c = text_[pos_++];
      if (c == '"') {
        return true;
      }
      if (static_cast<unsigned char>(c) < 0x20U) {
        return false;
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (at_end()) {
        return false;
      }
      const char e = text_[pos_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out.push_back(e);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t unit = 0;
          if (!hex4(unit)) {
            return false;
          }
          if (unit >= 0xD800U && unit <= 0xDBFFU) {
            std::uint32_t low = 0;
            if (!literal(R"(\u)") || !hex4(low) || low < 0xDC00U || low > 0xDFFFU) {
              return false;
            }
            unit = 0x10000U + ((unit - 0xD800U) << 10U) + (low - 0xDC00U);
          } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            return false;
          }
          append_utf8(out, unit);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  std::string_view text_;
  std::vector<JsonText::Node>& nodes_;
  std::size_t pos_ = 0;
};

}  // namespace

std::optional<JsonText> JsonText::parse(std::string_view text, std::size_t& error_offset) {
  JsonText result;
  Reader reader(text, result.nodes_);
  if (!reader.parse_document()) {
    error_offset = reader.offset();
    return std::nullopt;
  }
  return result;
}

const JsonText::Node* JsonText::member(const Node& object, std::string_view key) const {
  for (const auto& [name, index] : object.members) {
    if (name == key) {
      return &nodes_.at(index);
    }
  }
  return nullptr;
}

}  // namespace pdfkit::cli
