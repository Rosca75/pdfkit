// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The argument checks of the C ABI. A JSON parse error is re-raised with the
// symbol and the parameter prepended, keeping the reader's byte offset.

#include "api/arguments.hpp"

#include "platform/error.hpp"
#include "platform/json_reader.hpp"

#include <string>

namespace pdfkit::api {

namespace {

[[noreturn]] void refuse(std::string failed, std::string expected, std::string fix) {
  throw platform::Error(platform::ErrorCode::invalid_argument, std::move(failed),
                        std::move(expected), std::move(fix));
}

}  // namespace

void require_out(const pdfkit_buffer* out, std::string_view symbol, std::string_view parameter) {
  if (out == nullptr) {
    refuse(std::string(symbol) + " was called with a null " + std::string(parameter),
           "a pointer to a zeroed pdfkit_buffer",
           "declare a pdfkit_buffer initialised to zero and pass its address");
  }
}

void require_bytes(const std::uint8_t* bytes, std::size_t size, std::string_view symbol) {
  if (bytes == nullptr && size != 0) {
    refuse(
        std::string(symbol) + " was called with null bytes and a size of " + std::to_string(size),
        "a pointer to size bytes, or a null pointer with a size of 0",
        "pass the address of the PDF bytes, or a size of 0");
  }
}

std::string_view require_string(const char* text, std::string_view symbol,
                                std::string_view parameter) {
  if (text == nullptr) {
    refuse(std::string(symbol) + " was called with a null " + std::string(parameter),
           "a NUL-terminated UTF-8 string", "pass the " + std::string(parameter) + " argument");
  }
  return text;
}

platform::JsonValue parse_json_object(const char* json, std::string_view symbol,
                                      std::string_view parameter, bool required) {
  if (json == nullptr) {
    if (required) {
      refuse(std::string(symbol) + " was called with a null " + std::string(parameter),
             "a JSON object as docs/ABI.md describes for " + std::string(symbol),
             "pass the " + std::string(parameter) + " argument as a JSON object");
    }
    return platform::JsonValue::make_object({});
  }
  platform::JsonValue value;
  try {
    value = platform::JsonReader::parse(json);
  } catch (const platform::Error& error) {
    refuse(std::string(symbol) + " " + std::string(parameter) + ": " + error.failed(),
           error.expected(), error.fix());
  }
  if (value.kind() != platform::JsonValue::Kind::object) {
    refuse(std::string(symbol) + " " + std::string(parameter) + " is " +
               std::string(platform::JsonValue::kind_name(value.kind())),
           "a JSON object", "wrap the " + std::string(parameter) + " in a JSON object");
  }
  return value;
}

}  // namespace pdfkit::api
