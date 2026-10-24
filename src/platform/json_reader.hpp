// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// JsonReader reads a caller's JSON into a JsonValue tree, strictly: RFC 8259 and
// nothing more (no comments, no trailing commas, no byte order mark, no
// duplicate keys, well-formed UTF-8 only), with a depth limit of 64 and a size
// limit, because the input crosses the C ABI from callers we do not control.
// Every refusal is an invalid_argument error naming the byte offset, what was
// expected there and how to fix the input.

#ifndef PDFKIT_PLATFORM_JSON_READER_HPP
#define PDFKIT_PLATFORM_JSON_READER_HPP

#include "platform/json_value.hpp"

#include <cstddef>
#include <string_view>

namespace pdfkit::platform {

class JsonReader {
 public:
  struct Limits {
    std::size_t max_depth = 64;
    std::size_t max_bytes = std::size_t{64} * 1024 * 1024;
  };

  [[nodiscard]] static JsonValue parse(std::string_view text);
  [[nodiscard]] static JsonValue parse(std::string_view text, const Limits& limits);
};

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_JSON_READER_HPP
