// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The forward scan of reconstruction. Object headers are found by searching
// for "obj" and reading the two integers before it backwards, which finds
// every header in one pass whatever the bytes between objects hold.

#include "syntax/repair.hpp"

#include "syntax/lexer.hpp"
#include "syntax/parser.hpp"
#include "syntax/stream.hpp"

#include <optional>
#include <string_view>

namespace pdfkit::syntax {

namespace {

[[nodiscard]] std::optional<std::size_t> find_text(platform::ByteSpan bytes, std::size_t from,
                                                   std::string_view pattern) noexcept {
  if (from >= bytes.size()) {
    return std::nullopt;
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  const std::size_t found = text.find(pattern, from);
  return found == std::string_view::npos ? std::nullopt : std::optional<std::size_t>(found);
}

[[nodiscard]] bool is_digit(std::byte value) noexcept {
  const auto byte = std::to_integer<unsigned>(value);
  return byte >= '0' && byte <= '9';
}

// The start of "n g" before the "obj" at keyword, or no value when the bytes
// before it are not two integers separated and preceded by white space.
[[nodiscard]] std::optional<std::size_t> header_start(platform::ByteSpan bytes,
                                                      std::size_t keyword) noexcept {
  std::size_t at = keyword;
  const auto skip_space = [&]() {
    std::size_t skipped = 0;
    while (at > 0 && Lexer::is_white_space(bytes[at - 1])) {
      --at;
      ++skipped;
    }
    return skipped;
  };
  const auto skip_digits = [&]() {
    std::size_t skipped = 0;
    while (at > 0 && is_digit(bytes[at - 1]) && skipped < 12) {
      --at;
      ++skipped;
    }
    return skipped;
  };
  if (skip_space() == 0 || skip_digits() == 0 || skip_space() == 0 || skip_digits() == 0) {
    return std::nullopt;
  }
  if (at > 0 && Lexer::is_regular(bytes[at - 1])) {
    return std::nullopt;
  }
  return at;
}

}  // namespace

Repair::Found Repair::scan(platform::ByteSpan file) {
  Found found;
  const Parser parser(file);
  std::size_t position = 0;
  std::optional<std::size_t> next_object = find_text(file, 0, "obj");
  std::optional<std::size_t> next_trailer = Parser::find_keyword(file, 0, "trailer");
  while (next_object || next_trailer) {
    if (next_object && *next_object < position) {
      next_object = find_text(file, position, "obj");
      continue;
    }
    if (next_trailer && *next_trailer < position) {
      next_trailer = Parser::find_keyword(file, position, "trailer");
      continue;
    }
    if (next_trailer && (!next_object || *next_trailer < *next_object)) {
      std::vector<std::string> faults;
      const Object trailer = parser.parse_direct(*next_trailer + 7, faults);
      if (const Dictionary* dictionary = trailer.dictionary()) {
        found.trailers.push_back(*dictionary);
      }
      position = *next_trailer + 7;
      continue;
    }
    const std::size_t keyword = *next_object;
    position = keyword + 3;
    const bool bounded = keyword + 3 >= file.size() || !Lexer::is_regular(file[keyword + 3]);
    const std::optional<std::size_t> start = bounded ? header_start(file, keyword) : std::nullopt;
    if (!start) {
      continue;
    }
    const auto indirect = parser.parse_indirect(*start, {});
    if (!indirect) {
      continue;
    }
    found.objects[indirect->reference.number] = Location{*start, indirect->reference.generation};
    if (const Stream* stream = indirect->object.stream()) {
      if (stream->dictionary().get("Type").is_name("XRef")) {
        found.trailers.push_back(stream->dictionary());
      }
      position = indirect->end;
    } else if (indirect->repairs.empty()) {
      position = indirect->end;
    }
  }
  found.eof_found = find_text(file, 0, "%%EOF").has_value();
  return found;
}

}  // namespace pdfkit::syntax
