// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// A recursive-descent reader of the RFC 8259 grammar (recalled: value, object,
// array, string with its escapes and surrogate pairs, number, the three
// literals, and the four white-space characters). Recursion is bounded by the
// depth limit, so hostile nesting cannot exhaust the stack.

#include "platform/json_reader.hpp"

#include "platform/error.hpp"
#include "platform/utf8_sequence.hpp"

#include <charconv>
#include <cstdint>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace pdfkit::platform {

namespace {

constexpr std::string_view surrogate_pair_expected =
    R"(a surrogate pair written as \uD800-\uDBFF followed by \uDC00-\uDFFF)";
constexpr std::string_view escape_expected = R"(an escape: \" \\ \/ \b \f \n \r \t or \uXXXX)";

class Parser {
 public:
  Parser(std::string_view text, const JsonReader::Limits& limits) : text_(text), limits_(limits) {}

  JsonValue parse_document() {
    skip_space();
    JsonValue value = parse_value(0);
    skip_space();
    if (pos_ != text_.size()) {
      fail("found more input after the JSON value", "the end of the input after one JSON value");
    }
    return value;
  }

 private:
  [[noreturn]] void fail(std::string_view found, std::string_view expected) const {
    throw Error(ErrorCode::invalid_argument,
                "the JSON input is malformed at byte " + std::to_string(pos_) + ": " +
                    std::string(found),
                std::string(expected),
                "correct the input at byte " + std::to_string(pos_) +
                    "; it must be strict JSON (RFC 8259): double-quoted strings, no comments, no "
                    "trailing commas, no leading zeros, UTF-8 text");
  }

  [[nodiscard]] bool at_end() const noexcept { return pos_ >= text_.size(); }
  [[nodiscard]] char peek() const noexcept { return text_[pos_]; }

  void skip_space() noexcept {
    while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) {
      ++pos_;
    }
  }

  void expect(char wanted, const std::string& expected) {
    if (at_end() || peek() != wanted) {
      fail(at_end() ? "the input ends" : describe_here(), expected);
    }
    ++pos_;
  }

  [[nodiscard]] std::string describe_here() const {
    const auto byte = static_cast<unsigned char>(peek());
    if (byte >= 0x20U && byte < 0x7FU) {
      return std::string("found '") + peek() + "'";
    }
    return "found the byte " + std::to_string(byte);
  }

  JsonValue parse_value(std::size_t depth) {
    if (at_end()) {
      fail("the input ends where a value was expected",
           "a JSON value: an object, an array, a string, a number, true, false or null");
    }
    switch (peek()) {
      case '{':
        return parse_object(depth + 1);
      case '[':
        return parse_array(depth + 1);
      case '"':
        return JsonValue::make_string(parse_string());
      case 't':
        parse_literal("true");
        return JsonValue::make_boolean(true);
      case 'f':
        parse_literal("false");
        return JsonValue::make_boolean(false);
      case 'n':
        parse_literal("null");
        return {};
      default:
        if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
          return parse_number();
        }
        fail(describe_here() + " where a value was expected",
             "a JSON value: an object, an array, a string, a number, true, false or null");
    }
  }

  void check_depth(std::size_t depth) const {
    if (depth > limits_.max_depth) {
      fail("the input nests deeper than " + std::to_string(limits_.max_depth) + " levels",
           "objects and arrays nested at most " + std::to_string(limits_.max_depth) + " deep");
    }
  }

  JsonValue parse_object(std::size_t depth) {
    check_depth(depth);
    ++pos_;
    JsonValue::Object members;
    std::set<std::string, std::less<>> keys;
    skip_space();
    if (!at_end() && peek() == '}') {
      ++pos_;
      return JsonValue::make_object(std::move(members));
    }
    while (true) {
      skip_space();
      if (at_end() || peek() != '"') {
        fail(at_end() ? "the input ends inside an object" : describe_here() + " inside an object",
             "a member name in double quotes");
      }
      const std::size_t key_at = pos_;
      std::string key = parse_string();
      if (keys.contains(key)) {
        pos_ = key_at;
        fail("the key \"" + key + "\" appears twice in one object", "each key once per object");
      }
      keys.insert(key);
      skip_space();
      expect(':', "':' after a member name");
      skip_space();
      JsonValue value = parse_value(depth);
      members.emplace_back(std::move(key), std::move(value));
      skip_space();
      if (at_end()) {
        fail("the input ends inside an object", "',' or '}'");
      }
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      expect('}', "',' or '}' after an object member");
      return JsonValue::make_object(std::move(members));
    }
  }

  JsonValue parse_array(std::size_t depth) {
    check_depth(depth);
    ++pos_;
    JsonValue::Array items;
    skip_space();
    if (!at_end() && peek() == ']') {
      ++pos_;
      return JsonValue::make_array(std::move(items));
    }
    while (true) {
      skip_space();
      items.push_back(parse_value(depth));
      skip_space();
      if (at_end()) {
        fail("the input ends inside an array", "',' or ']'");
      }
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      expect(']', "',' or ']' after an array element");
      return JsonValue::make_array(std::move(items));
    }
  }

  void parse_literal(std::string_view literal) {
    if (text_.substr(pos_, literal.size()) != literal) {
      fail(describe_here() + " where a value was expected",
           "a JSON value: an object, an array, a string, a number, true, false or null");
    }
    pos_ += literal.size();
  }

  JsonValue parse_number() {
    const std::size_t start = pos_;
    bool integral = true;
    if (peek() == '-') {
      ++pos_;
    }
    if (at_end() || !is_digit(peek())) {
      fail(at_end() ? "the input ends inside a number" : describe_here() + " inside a number",
           "a digit after '-'");
    }
    if (peek() == '0') {
      ++pos_;
      if (!at_end() && is_digit(peek())) {
        fail("a number with a leading zero", "a number without leading zeros, such as 7 or 0.7");
      }
    } else {
      skip_digits();
    }
    if (!at_end() && peek() == '.') {
      integral = false;
      ++pos_;
      require_digit("a digit after the decimal point");
      skip_digits();
    }
    if (!at_end() && (peek() == 'e' || peek() == 'E')) {
      integral = false;
      ++pos_;
      if (!at_end() && (peek() == '+' || peek() == '-')) {
        ++pos_;
      }
      require_digit("a digit in the exponent");
      skip_digits();
    }
    const std::string_view lexeme = text_.substr(start, pos_ - start);
    const char* first = lexeme.data();
    const char* last = lexeme.data() + lexeme.size();
    if (integral) {
      std::int64_t exact = 0;
      const auto result = std::from_chars(first, last, exact);
      if (result.ec == std::errc{} && result.ptr == last) {
        return JsonValue::make_integer(exact);
      }
    }
    double value = 0.0;
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
      pos_ = start;
      fail("the number " + std::string(lexeme) + " is out of the range of a double",
           "a number between about -1.8e308 and 1.8e308");
    }
    return JsonValue::make_number(value);
  }

  static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

  void skip_digits() noexcept {
    while (!at_end() && is_digit(peek())) {
      ++pos_;
    }
  }

  void require_digit(const std::string& expected) {
    if (at_end() || !is_digit(peek())) {
      fail(at_end() ? "the input ends inside a number" : describe_here() + " inside a number",
           expected);
    }
  }

  std::uint32_t parse_hex4() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      if (at_end()) {
        fail("the input ends inside a \\u escape", "four hexadecimal digits after \\u");
      }
      const char c = peek();
      std::uint32_t digit = 0;
      if (c >= '0' && c <= '9') {
        digit = static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        digit = static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        digit = static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        fail(describe_here() + " inside a \\u escape", "four hexadecimal digits after \\u");
      }
      value = (value << 4U) | digit;
      ++pos_;
    }
    return value;
  }

  void parse_unicode_escape(std::string& out) {
    const std::size_t escape_at = pos_ - 2;
    std::uint32_t unit = parse_hex4();
    if (unit >= 0xDC00U && unit <= 0xDFFFU) {
      pos_ = escape_at;
      fail(R"(a low surrogate \u escape without a high surrogate before it)",
           surrogate_pair_expected);
    }
    if (unit >= 0xD800U && unit <= 0xDBFFU) {
      if (text_.substr(pos_, 2) != R"(\u)") {
        pos_ = escape_at;
        fail(R"(a high surrogate \u escape without a low surrogate after it)",
             surrogate_pair_expected);
      }
      pos_ += 2;
      const std::uint32_t low = parse_hex4();
      if (low < 0xDC00U || low > 0xDFFFU) {
        pos_ = escape_at;
        fail(R"(a high surrogate \u escape followed by something other than a low surrogate)",
             surrogate_pair_expected);
      }
      unit = 0x10000U + ((unit - 0xD800U) << 10U) + (low - 0xDC00U);
    }
    append_utf8(out, static_cast<char32_t>(unit));
  }

  std::string parse_string() {
    ++pos_;
    std::string out;
    while (true) {
      if (at_end()) {
        fail("the input ends inside a string", "a closing '\"'");
      }
      const char c = peek();
      if (c == '"') {
        ++pos_;
        return out;
      }
      if (static_cast<unsigned char>(c) < 0x20U) {
        fail("a raw control character inside a string",
             "control characters written as escapes, such as \\n or \\u0001");
      }
      if (c == '\\') {
        ++pos_;
        if (at_end()) {
          fail("the input ends inside an escape", escape_expected);
        }
        const char e = peek();
        ++pos_;
        switch (e) {
          case '"':
            out.push_back('"');
            break;
          case '\\':
            out.push_back('\\');
            break;
          case '/':
            out.push_back('/');
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
          case 'u':
            parse_unicode_escape(out);
            break;
          default:
            pos_ -= 2;
            fail(R"(an unknown escape \)" + std::string(1, e), escape_expected);
        }
        continue;
      }
      const Utf8Sequence sequence = next_utf8_sequence(text_, pos_);
      if (!sequence.valid) {
        fail("a byte sequence that is not well-formed UTF-8", "UTF-8 text");
      }
      out.append(text_.substr(pos_, sequence.length));
      pos_ += sequence.length;
    }
  }

  std::string_view text_;
  const JsonReader::Limits& limits_;
  std::size_t pos_ = 0;
};

}  // namespace

JsonValue JsonReader::parse(std::string_view text) {
  return parse(text, Limits{});
}

JsonValue JsonReader::parse(std::string_view text, const Limits& limits) {
  if (text.size() > limits.max_bytes) {
    throw Error(ErrorCode::invalid_argument,
                "the JSON input is " + std::to_string(text.size()) +
                    " bytes, more than the limit of " + std::to_string(limits.max_bytes) + " bytes",
                "JSON input of at most " + std::to_string(limits.max_bytes) + " bytes",
                "split the request into smaller calls");
  }
  Parser parser(text, limits);
  return parser.parse_document();
}

}  // namespace pdfkit::platform
