// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The cross-reference stream's rows, big-endian per field (ISO 32000-1 section
// 7.5.8.2, read), and its dictionary in a fixed key order.

#include "writer/xref_stream_writer.hpp"

#include "platform/byte_span.hpp"
#include "writer/document_id.hpp"
#include "writer/stream_copier.hpp"

namespace pdfkit::writer {

namespace {

void append_be(std::vector<std::byte>& out, std::uint64_t value, int width) {
  for (int i = width - 1; i >= 0; --i) {
    out.push_back(static_cast<std::byte>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
  }
}

}  // namespace

int XrefStreamWriter::width_for(std::uint64_t value) noexcept {
  int width = 1;
  while (width < 8 && (value >> (8U * static_cast<unsigned>(width))) != 0) {
    ++width;
  }
  return width;
}

XrefStreamWriter::Placed XrefStreamWriter::write(std::string& out,
                                                 const std::vector<std::uint64_t>& offsets,
                                                 std::uint32_t root,
                                                 std::optional<std::uint32_t> info) {
  const auto own_number = static_cast<std::uint32_t>(offsets.size() + 1);
  const std::uint64_t own_offset = out.size();
  std::uint64_t largest = own_offset;
  for (const std::uint64_t offset : offsets) {
    largest = std::max(largest, offset);
  }
  Placed placed;
  placed.offset_width = width_for(largest);
  const int width = placed.offset_width;

  std::vector<std::byte> rows;
  rows.reserve((offsets.size() + 2) * static_cast<std::size_t>(width + 3));
  append_be(rows, 0, 1);
  append_be(rows, 0, width);
  append_be(rows, 65535, 2);
  for (const std::uint64_t offset : offsets) {
    append_be(rows, 1, 1);
    append_be(rows, offset, width);
    append_be(rows, 0, 2);
  }
  append_be(rows, 1, 1);
  append_be(rows, own_offset, width);
  append_be(rows, 0, 2);
  const std::vector<std::byte> data = StreamCopier::deflate(rows);

  const std::string size = std::to_string(own_number + 1);
  out += std::to_string(own_number);
  out += " 0 obj\n<< /Type /XRef /Size ";
  out += size;
  out += " /Index [0 ";
  out += size;
  out += "] /W [1 ";
  out += std::to_string(width);
  out += " 2] /Root ";
  out += std::to_string(root);
  out += " 0 R";
  if (info) {
    out += " /Info ";
    out += std::to_string(*info);
    out += " 0 R";
  }
  out += " /ID [<";
  placed.id_first = out.size();
  out += DocumentId::placeholder();
  out += "> <";
  placed.id_second = out.size();
  out += DocumentId::placeholder();
  out += ">] /Filter /FlateDecode /Length ";
  out += std::to_string(data.size());
  out += " >>\nstream\n";
  out.append(reinterpret_cast<const char*>(data.data()), data.size());
  out += "\nendstream\nendobj\nstartxref\n";
  out += std::to_string(own_offset);
  out += "\n%%EOF\n";
  return placed;
}

}  // namespace pdfkit::writer
