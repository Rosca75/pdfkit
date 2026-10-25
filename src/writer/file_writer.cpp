// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The file layout: "n 0 obj" and a newline, the object, and for a stream its
// dictionary, "stream", a newline, the data, a newline, "endstream"; then a
// newline and "endobj" and a newline. The newline before endstream is not
// counted in /Length (ISO 32000-1 section 7.3.8.1, read).

#include "writer/file_writer.hpp"

#include "platform/error.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "syntax/stream.hpp"
#include "writer/document_id.hpp"
#include "writer/reachability.hpp"
#include "writer/renumbering.hpp"
#include "writer/serialiser.hpp"
#include "writer/stream_copier.hpp"
#include "writer/xref_stream_writer.hpp"

#include <charconv>
#include <optional>
#include <system_error>
#include <vector>

namespace pdfkit::writer {

using syntax::Object;

namespace {

std::optional<double> version_value(const std::string& text) {
  double value = 0.0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::fixed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::string FileWriter::header_version(const std::string& version) {
  const std::optional<double> value = version_value(version);
  if (!value || *value <= 1.5 || *value >= 10.0) {
    return "1.5";
  }
  return version;
}

FileWriter::Result FileWriter::write(ObjectGraph& graph) {
  const std::uint32_t root = graph.root();
  if (root == 0 || graph.get(root).dictionary() == nullptr) {
    throw platform::Error(platform::ErrorCode::internal, "the writer has no catalog to write",
                          "a document whose trailer names its catalog",
                          "this is a defect in pdfkit; report it with the input file");
  }
  const Reachability reachable(graph);
  const Renumbering numbers(reachable.order());
  const Serialiser::Numbering numbering =
      [&numbers](Object::Reference reference) -> std::optional<std::uint32_t> {
    return numbers.find(reference.number);
  };

  Result result;
  std::string& out = result.bytes;
  out += "%PDF-";
  out += header_version(graph.document().version());
  out += "\n%\xE2\xE3\xCF\xD3\n";

  // The catalog's XMP packet, whose identifier placeholders are patched
  // with the /ID.
  std::uint32_t packet = 0;
  if (const syntax::Dictionary* catalog = graph.get(root).dictionary()) {
    if (const auto reference = catalog->get("Metadata").reference()) {
      packet = reference->number;
    }
  }
  std::size_t packet_begin = 0;
  std::size_t packet_end = 0;

  std::vector<std::uint64_t> offsets;
  offsets.reserve(reachable.order().size());
  for (const std::uint32_t number : reachable.order()) {
    offsets.push_back(out.size());
    const Object value = graph.get(number);
    out += std::to_string(offsets.size());
    out += " 0 obj\n";
    if (const syntax::Stream* stream = value.stream()) {
      const StreamCopier::Copy copy = StreamCopier::copy(graph.document(), *stream);
      Serialiser::write(out, Object::make_dictionary(copy.dictionary), numbering);
      out += "\nstream\n";
      if (number == packet) {
        packet_begin = out.size();
        packet_end = out.size() + copy.data.size();
      }
      out.append(reinterpret_cast<const char*>(copy.data.data()), copy.data.size());
      out += "\nendstream";
    } else {
      Serialiser::write(out, value, numbering);
    }
    out += "\nendobj\n";
  }

  std::optional<std::uint32_t> info;
  if (const auto reference = graph.trailer().get("Info").reference()) {
    info = numbers.find(reference->number);
  }
  // The catalog is reached first, so it is object 1.
  const XrefStreamWriter::Placed placed = XrefStreamWriter::write(out, offsets, 1, info);
  result.id = DocumentId::patch(out, placed.id_first, placed.id_second, packet_begin, packet_end);
  result.objects = offsets.size();
  result.missing_references = reachable.missing();
  return result;
}

}  // namespace pdfkit::writer
