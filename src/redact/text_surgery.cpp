// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The rebuild of one text-showing operator. Its records are grouped by the
// operand holding them; each string is walked code by code from its first
// byte, each record taking code_length bytes, and the code those bytes spell
// is compared with the record's, so a record that does not sit where the walk
// expects it is caught here rather than cutting the wrong bytes. The kept
// codes of a string stay in one piece, written in the string's own form
// (literal or hexadecimal); bytes past the last record (a truncated
// multi-byte code, which drew nothing) stay at the end of the last piece.

#include "redact/text_surgery.hpp"

#include "redact/content_syntax.hpp"

#include <cmath>
#include <variant>

namespace pdfkit::redact {

namespace {

using Operation = syntax::ContentLexer::Operation;

// One item of the rebuilt TJ array: a string piece or a number.
struct Piece {
  std::string bytes;
  bool hex = false;
};
using Item = std::variant<Piece, double>;

void push_number(std::vector<Item>& items, double value) {
  if (!items.empty()) {
    if (double* last = std::get_if<double>(&items.back())) {
      *last += value;
      return;
    }
  }
  items.emplace_back(value);
}

void push_piece(std::vector<Item>& items, Piece& piece) {
  if (!piece.bytes.empty()) {
    items.emplace_back(std::move(piece));
  }
  piece = Piece{{}, piece.hex};
}

[[nodiscard]] bool unplaced(const text::GlyphRecord& record, const Operation& operation) {
  return record.code_start == operation.start && record.code_end == operation.operator_start;
}

[[nodiscard]] std::uint32_t code_of(const std::string& bytes, std::size_t at, std::size_t length) {
  std::uint32_t code = 0;
  for (std::size_t i = 0; i < length; ++i) {
    code = (code << 8U) | static_cast<unsigned char>(bytes[at + i]);
  }
  return code;
}

// Appends the pieces and numbers of one string; false when a record does
// not read back from the string's bytes.
[[nodiscard]] bool cut(const syntax::Object::String& string,
                       const std::vector<const text::GlyphRecord*>& records,
                       const std::set<std::size_t>& removed, std::vector<Item>& items) {
  Piece piece{{}, string.hex};
  std::size_t offset = 0;
  for (const text::GlyphRecord* record : records) {
    const std::size_t length = record->code_length;
    if (length == 0 || length > 4 || offset + length > string.bytes.size() ||
        code_of(string.bytes, offset, length) != record->code) {
      return false;
    }
    if (removed.contains(record->draw)) {
      push_piece(items, piece);
      push_number(items, TextSurgery::hole_number(*record));
    } else {
      piece.bytes.append(string.bytes, offset, length);
    }
    offset += length;
  }
  piece.bytes.append(string.bytes, offset);
  push_piece(items, piece);
  return true;
}

[[nodiscard]] std::string written(const std::vector<Item>& items) {
  std::string out = "[";
  bool first = true;
  for (const Item& item : items) {
    if (!first) {
      out += ' ';
    }
    first = false;
    if (const Piece* piece = std::get_if<Piece>(&item)) {
      out += piece->hex ? ContentSyntax::hex(piece->bytes) : ContentSyntax::literal(piece->bytes);
    } else {
      out += ContentSyntax::number(std::get<double>(item));
    }
  }
  return out + "] TJ";
}

// What precedes the TJ: the line advance of ' and the spacing and line
// advance of ".
[[nodiscard]] std::string prefix(const Operation& operation) {
  if (operation.op == "'") {
    return "T* ";
  }
  if (operation.op != "\"") {
    return {};
  }
  std::string out;
  if (!operation.operands.empty()) {
    if (const auto value = operation.operands[0].number(); value && std::isfinite(*value)) {
      out += ContentSyntax::number(*value) + " Tw ";
    }
  }
  if (operation.operands.size() > 1) {
    if (const auto value = operation.operands[1].number(); value && std::isfinite(*value)) {
      out += ContentSyntax::number(*value) + " Tc ";
    }
  }
  return out + "T* ";
}

}  // namespace

double TextSurgery::hole_number(const text::GlyphRecord& record) {
  const double scale = record.vertical ? 1.0 : record.horizontal_scaling / 100.0;
  const double denominator = record.font_size * scale;
  if (denominator == 0.0 || !std::isfinite(denominator) || !std::isfinite(record.advance)) {
    return 0.0;
  }
  return -record.advance / denominator * 1000.0;
}

TextSurgery::Plan TextSurgery::plan(const text::ContentStream& stream, std::size_t stream_index,
                                    std::span<const text::GlyphRecord> records,
                                    const std::set<std::size_t>& removed) {
  Plan result;
  std::map<std::size_t, std::vector<const text::GlyphRecord*>> by_operation;
  for (const text::GlyphRecord& record : records) {
    if (record.stream == stream_index) {
      by_operation[record.operation].push_back(&record);
    }
  }
  for (const auto& [index, glyphs] : by_operation) {
    bool touched = false;
    for (const text::GlyphRecord* record : glyphs) {
      touched = touched || removed.contains(record->draw);
    }
    if (!touched || index >= stream.operations.size()) {
      continue;
    }
    const Operation& operation = stream.operations[index];
    std::map<std::size_t, std::vector<const text::GlyphRecord*>> by_operand;
    bool placed = true;
    for (const text::GlyphRecord* record : glyphs) {
      by_operand[record->operand].push_back(record);
      placed = placed && !unplaced(*record, operation);
    }
    std::vector<Item> items;
    if (placed && operation.op == "TJ" && !operation.operands.empty()) {
      const syntax::Object::Array* array = operation.operands.back().array();
      placed = array != nullptr;
      for (std::size_t j = 0; placed && j < array->size(); ++j) {
        const syntax::Object& item = (*array)[j];
        if (const auto value = item.number(); value && std::isfinite(*value)) {
          push_number(items, *value);
        } else if (const syntax::Object::String* string = item.string()) {
          placed = cut(*string, by_operand[j], removed, items);
        }
      }
    } else if (placed) {
      const std::size_t shown =
          operation.op == "\"" ? 2 : (operation.operands.empty() ? 0 : operation.operands.size() - 1);
      const syntax::Object::String* string =
          shown < operation.operands.size() ? operation.operands[shown].string() : nullptr;
      placed = string != nullptr && cut(*string, by_operand[shown], removed, items);
    }
    if (!placed) {
      items.clear();
      double total = 0.0;
      for (const text::GlyphRecord* record : glyphs) {
        total += hole_number(*record);
        if (!removed.contains(record->draw)) {
          result.widened.insert(record->draw);
        }
      }
      items.emplace_back(total);
      result.notes.push_back("the codes of operator " + std::to_string(index) +
                             " could not be cut one by one, so every glyph it shows was removed");
    }
    result.edits[index] = prefix(operation) + written(items);
  }
  return result;
}

}  // namespace pdfkit::redact
