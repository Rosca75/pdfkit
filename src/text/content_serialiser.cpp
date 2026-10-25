// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The writer of a walk. Records are grouped by operation and ordered by their
// first byte; an operation without records is copied whole. A record whose
// range carries the operator's whole operand span (the interpreter's mark of a
// string it could not place) is not checked byte by byte and writes nothing
// of its own, so such an operation is copied as it was.

#include "text/content_serialiser.hpp"

#include "text/string_operands.hpp"

#include <algorithm>
#include <string_view>

namespace pdfkit::text {

namespace {

void append(std::vector<std::byte>& out, const std::vector<std::byte>& bytes, std::size_t from,
            std::size_t to) {
  to = std::min(to, bytes.size());
  if (from < to) {
    out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(from),
               bytes.begin() + static_cast<std::ptrdiff_t>(to));
  }
}

[[nodiscard]] std::string code_bytes(const GlyphRecord& record) {
  std::string out;
  for (std::size_t i = record.code_length; i > 0; --i) {
    out.push_back(static_cast<char>((record.code >> (8U * (i - 1))) & 0xFFU));
  }
  return out;
}

}  // namespace

ContentSerialiser::Result ContentSerialiser::write(const ContentStream& stream,
                                                   std::size_t stream_index,
                                                   std::span<const GlyphRecord> records,
                                                   const std::map<std::size_t, std::string>& edits) {
  Result result;
  if (stream.bytes == nullptr) {
    return result;
  }
  const std::vector<std::byte>& bytes = *stream.bytes;
  std::map<std::size_t, std::vector<const GlyphRecord*>> by_operation;
  for (const GlyphRecord& record : records) {
    if (record.stream == stream_index) {
      by_operation[record.operation].push_back(&record);
    }
  }
  result.bytes.reserve(bytes.size());
  std::size_t cursor = 0;
  for (std::size_t index = 0; index < stream.operations.size(); ++index) {
    const syntax::ContentLexer::Operation& operation = stream.operations[index];
    append(result.bytes, bytes, cursor, operation.start);
    cursor = std::max(cursor, operation.start);
    if (const auto edit = edits.find(index); edit != edits.end()) {
      for (const char value : edit->second) {
        result.bytes.push_back(static_cast<std::byte>(value));
      }
      cursor = std::max(cursor, operation.end);
      continue;
    }
    const auto found = by_operation.find(index);
    if (found == by_operation.end()) {
      append(result.bytes, bytes, cursor, operation.end);
      cursor = std::max(cursor, operation.end);
      continue;
    }
    std::vector<const GlyphRecord*>& glyphs = found->second;
    std::ranges::stable_sort(glyphs, {}, &GlyphRecord::code_start);
    for (const GlyphRecord* record : glyphs) {
      const bool unplaced =
          record->code_start == operation.start && record->code_end == operation.operator_start;
      if (unplaced) {
        continue;
      }
      if (record->code_start < cursor || record->code_end < record->code_start ||
          record->code_end > operation.operator_start) {
        result.mismatches.push_back("glyph " + std::to_string(record->draw) + " of operator " +
                                    std::to_string(index) +
                                    " overlaps the bytes before it or leaves its operands");
        continue;
      }
      append(result.bytes, bytes, cursor, record->code_start);
      const platform::ByteSpan fragment(bytes.data() + record->code_start,
                                        record->code_end - record->code_start);
      if (StringOperands::decode_fragment(fragment, record->hex_string) != code_bytes(*record)) {
        result.mismatches.push_back("glyph " + std::to_string(record->draw) + " of operator " +
                                    std::to_string(index) + " names bytes that are not its code");
      }
      append(result.bytes, bytes, record->code_start, record->code_end);
      cursor = record->code_end;
    }
    append(result.bytes, bytes, cursor, operation.end);
    cursor = std::max(cursor, operation.end);
  }
  append(result.bytes, bytes, cursor, bytes.size());
  return result;
}

}  // namespace pdfkit::text
