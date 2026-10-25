// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Object syntax out. Containers nest only as deep as the parser lets them
// (64 levels), so the recursion here is bounded by the reader, not by the
// file.

#include "writer/serialiser.hpp"

#include "platform/error.hpp"
#include "syntax/dictionary.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>

namespace pdfkit::writer {

using syntax::Object;

namespace {

constexpr std::string_view hex_digits = "0123456789ABCDEF";

[[nodiscard]] bool is_delimiter(unsigned char byte) noexcept {
  return byte == '(' || byte == ')' || byte == '<' || byte == '>' || byte == '[' || byte == ']' ||
         byte == '{' || byte == '}' || byte == '/' || byte == '%';
}

void write_literal(std::string& out, const std::string& bytes) {
  out += '(';
  for (const char c : bytes) {
    const auto byte = static_cast<unsigned char>(c);
    switch (byte) {
      case '(':
      case ')':
      case '\\':
        out += '\\';
        out += c;
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      default:
        if (byte < 0x20 || byte > 0x7E) {
          // Three octal digits (ISO 32000-1 section 7.3.4.2), so a following
          // digit is never read as part of the escape.
          out += '\\';
          constexpr std::string_view octal = "01234567";
          const unsigned value = byte;
          out += octal[(value >> 6U) & 7U];
          out += octal[(value >> 3U) & 7U];
          out += octal[value & 7U];
        } else {
          out += c;
        }
    }
  }
  out += ')';
}

void write_hex(std::string& out, const std::string& bytes) {
  out += '<';
  for (const char c : bytes) {
    const auto byte = static_cast<unsigned char>(c);
    out += hex_digits[byte >> 4U];
    out += hex_digits[byte & 0x0FU];
  }
  out += '>';
}

}  // namespace

void Serialiser::write_number(std::string& out, const Object::Number& number) {
  std::array<char, 400> buffer{};
  if (number.is_integer) {
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number.integer);
    out.append(buffer.data(), result.ptr);
    return;
  }
  const double value = number.value;
  if (!std::isfinite(value) || value == 0.0) {
    // The syntax has no infinity and no negative zero; both read as 0.
    out += "0.0";
    return;
  }
  const auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed);
  if (result.ec != std::errc{}) {
    out += "0.0";
    return;
  }
  const std::string_view text(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
  out += text;
  if (text.find('.') == std::string_view::npos) {
    out += ".0";
  }
}

void Serialiser::write_name(std::string& out, std::string_view name) {
  out += '/';
  for (const char c : name) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x21 || byte > 0x7E || byte == '#' || is_delimiter(byte)) {
      out += '#';
      out += hex_digits[byte >> 4U];
      out += hex_digits[byte & 0x0FU];
    } else {
      out += c;
    }
  }
}

void Serialiser::write_string(std::string& out, const Object::String& string) {
  if (string.hex) {
    write_hex(out, string.bytes);
  } else {
    write_literal(out, string.bytes);
  }
}

void Serialiser::write(std::string& out, const Object& object, const Numbering& numbering) {
  switch (object.kind()) {
    case Object::Kind::null:
      out += "null";
      return;
    case Object::Kind::boolean:
      out += object.boolean().value_or(false) ? "true" : "false";
      return;
    case Object::Kind::number:
      write_number(out, *object.number_value());
      return;
    case Object::Kind::string:
      write_string(out, *object.string());
      return;
    case Object::Kind::name:
      write_name(out, *object.name());
      return;
    case Object::Kind::array: {
      out += '[';
      bool first = true;
      for (const Object& item : *object.array()) {
        if (!first) {
          out += ' ';
        }
        first = false;
        write(out, item, numbering);
      }
      out += ']';
      return;
    }
    case Object::Kind::dictionary: {
      out += "<<";
      for (const auto& [key, value] : object.dictionary()->entries()) {
        out += ' ';
        write_name(out, key);
        out += ' ';
        write(out, value, numbering);
      }
      out += " >>";
      return;
    }
    case Object::Kind::reference: {
      const auto reference = object.reference();
      const std::optional<std::uint32_t> number =
          reference ? numbering(*reference) : std::optional<std::uint32_t>();
      if (!number) {
        out += "null";
        return;
      }
      out += std::to_string(*number);
      out += " 0 R";
      return;
    }
    case Object::Kind::stream:
      break;
  }
  throw platform::Error(platform::ErrorCode::internal, "the writer met a stream as a direct object",
                        "every stream written as an indirect object of its own",
                        "this is a defect in pdfkit; report it with the input file");
}

}  // namespace pdfkit::writer
