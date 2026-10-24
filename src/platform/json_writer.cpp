// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The JSON writer's state machine and its escaping. The escapes are those of
// RFC 8259 section 7 (recalled): quotation mark, reverse solidus and the
// control characters below U+0020, with the two-character forms where the RFC
// has one and \u00XX otherwise.

#include "platform/json_writer.hpp"

#include "platform/error.hpp"
#include "platform/utf8_sequence.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <system_error>

namespace pdfkit::platform {

namespace {

[[noreturn]] void misuse(const std::string& what) {
  throw Error(ErrorCode::internal, "the JSON writer was asked to " + what,
              "calls that form one valid JSON value",
              "this is a defect in pdfkit; report it with the input that caused it");
}

void require_finite(double value) {
  if (!std::isfinite(value)) {
    throw Error(ErrorCode::internal, "a computed number is not finite and has no JSON form",
                "a finite number",
                "this is a defect in pdfkit; report it with the input that caused it");
  }
}

template <typename... Args>
void append_chars(std::string& out, Args... args) {
  std::array<char, 400> chars{};
  const auto result = std::to_chars(chars.data(), chars.data() + chars.size(), args...);
  if (result.ec != std::errc{}) {
    misuse("format a number that does not fit its buffer");
  }
  out.append(chars.data(), result.ptr);
}

}  // namespace

void JsonWriter::before_value() {
  if (stack_.empty()) {
    if (root_written_) {
      misuse("write a second top-level value");
    }
    root_written_ = true;
    return;
  }
  Frame& frame = stack_.back();
  if (frame.is_object) {
    if (!frame.awaiting_value) {
      misuse("write an object member without its key");
    }
    frame.awaiting_value = false;
    return;
  }
  if (!frame.empty) {
    out_.push_back(',');
  }
  frame.empty = false;
}

JsonWriter& JsonWriter::begin_object() {
  before_value();
  stack_.push_back({true, true, false});
  out_.push_back('{');
  return *this;
}

JsonWriter& JsonWriter::end_object() {
  if (stack_.empty() || !stack_.back().is_object || stack_.back().awaiting_value) {
    misuse("close an object that is not open or whose last key has no value");
  }
  stack_.pop_back();
  out_.push_back('}');
  return *this;
}

JsonWriter& JsonWriter::begin_array() {
  before_value();
  stack_.push_back({false, true, false});
  out_.push_back('[');
  return *this;
}

JsonWriter& JsonWriter::end_array() {
  if (stack_.empty() || stack_.back().is_object) {
    misuse("close an array that is not open");
  }
  stack_.pop_back();
  out_.push_back(']');
  return *this;
}

JsonWriter& JsonWriter::key(std::string_view name) {
  if (stack_.empty() || !stack_.back().is_object || stack_.back().awaiting_value) {
    misuse("write a key outside an object or after another key");
  }
  Frame& frame = stack_.back();
  if (!frame.empty) {
    out_.push_back(',');
  }
  frame.empty = false;
  write_string(name);
  out_.push_back(':');
  frame.awaiting_value = true;
  return *this;
}

JsonWriter& JsonWriter::string(std::string_view text) {
  before_value();
  write_string(text);
  return *this;
}

JsonWriter& JsonWriter::boolean(bool value) {
  before_value();
  out_.append(value ? "true" : "false");
  return *this;
}

JsonWriter& JsonWriter::null() {
  before_value();
  out_.append("null");
  return *this;
}

JsonWriter& JsonWriter::integer(std::int64_t value) {
  before_value();
  append_chars(out_, value);
  return *this;
}

JsonWriter& JsonWriter::unsigned_integer(std::uint64_t value) {
  before_value();
  append_chars(out_, value);
  return *this;
}

JsonWriter& JsonWriter::number(double value) {
  require_finite(value);
  before_value();
  append_chars(out_, value == 0.0 ? 0.0 : value);
  return *this;
}

JsonWriter& JsonWriter::fixed(double value, int decimals) {
  require_finite(value);
  if (decimals < 0 || decimals > 17) {
    misuse("write a number with " + std::to_string(decimals) + " decimals (0 to 17 are allowed)");
  }
  before_value();
  std::string digits;
  append_chars(digits, value, std::chars_format::fixed, decimals);
  if (digits.front() == '-' && digits.find_first_not_of("-0.") == std::string::npos) {
    digits.erase(0, 1);
  }
  out_.append(digits);
  return *this;
}

bool JsonWriter::complete() const noexcept {
  return root_written_ && stack_.empty();
}

std::string JsonWriter::take() {
  if (!complete()) {
    misuse("hand back a value that is not complete");
  }
  std::string text = std::move(out_);
  out_.clear();
  root_written_ = false;
  return text;
}

void JsonWriter::write_string(std::string_view text) {
  static constexpr std::string_view hex = "0123456789abcdef";
  out_.push_back('"');
  std::size_t at = 0;
  while (at < text.size()) {
    const Utf8Sequence sequence = next_utf8_sequence(text, at);
    if (!sequence.valid) {
      out_.append("\xEF\xBF\xBD");
    } else if (sequence.length > 1) {
      out_.append(text.substr(at, sequence.length));
    } else {
      const char c = text[at];
      switch (c) {
        case '"':
          out_.append("\\\"");
          break;
        case '\\':
          out_.append("\\\\");
          break;
        case '\b':
          out_.append("\\b");
          break;
        case '\f':
          out_.append("\\f");
          break;
        case '\n':
          out_.append("\\n");
          break;
        case '\r':
          out_.append("\\r");
          break;
        case '\t':
          out_.append("\\t");
          break;
        default:
          if (static_cast<unsigned char>(c) < 0x20U) {
            const auto code = static_cast<unsigned char>(c);
            out_.append("\\u00");
            out_.push_back(hex[code >> 4U]);
            out_.push_back(hex[code & 0x0FU]);
          } else {
            out_.push_back(c);
          }
          break;
      }
    }
    at += sequence.length;
  }
  out_.push_back('"');
}

}  // namespace pdfkit::platform
