// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// MetadataWriter is the metadata stage of a write, docs/DESIGN.md section 10.
// A field the caller set is written into both places: the Info dictionary
// (PDFDocEncoding when every character has a code, UTF-16BE with the mark
// otherwise; a date as a PDF date) and every XMP property the field pairs
// with (a date in ISO 8601), the packet edited by splicing the scanned
// ranges and keeping every other byte, so custom namespaces survive. A
// property the packet lacks is added in an rdf:Description of its own before
// the rdf:RDF end tag; a field set to no value is removed from both places;
// a field not set keeps its value in both. Whatever the caller set, the
// packet loses every property of the media-management namespace but its
// identifiers, whose values become placeholders the file writer patches from
// the output's hash (writer::DocumentId), because the original's history and
// identifiers identify the original; an Info dictionary is created only when
// a field is set. A packet that does not scan is dropped with the warning
// xmp_unreadable rather than guessed at; with no packet, none is created.

#ifndef PDFKIT_WRITER_METADATA_WRITER_HPP
#define PDFKIT_WRITER_METADATA_WRITER_HPP

#include "syntax/metadata_reader.hpp"
#include "syntax/xmp_scanner.hpp"
#include "writer/channel_table.hpp"
#include "writer/object_graph.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::writer {

class MetadataWriter {
 public:
  using Field = syntax::MetadataReader::Field;
  // The value per field the caller set; no value removes the field.
  using Values = std::map<Field, std::optional<std::string>>;

  // How one field is written.
  struct Written {
    Field field = Field::title;
    bool removed = false;
    // The Info value as text, and the form its string is stored in.
    std::string info;
    std::string info_encoding;
    // The XMP value as text.
    std::string xmp;
  };

  // How values are written: the Info text, its encoding and the XMP text of
  // each field.
  [[nodiscard]] static std::vector<Written> preview(const Values& values);

  // Applies values to graph's Info dictionary and catalog packet and
  // replaces the packet's identifiers with their placeholders; the warnings
  // met (xmp_unreadable) are appended to warnings.
  static void apply(ObjectGraph& graph, const Values& values,
                    std::vector<ChannelTable::Warning>& warnings);

  // packet with values applied and the media-management properties edited,
  // as apply writes it; scanned is the packet's scan, which must be readable.
  [[nodiscard]] static std::string rewrite_packet(std::string_view packet,
                                                  const syntax::XmpScanner::Result& scanned,
                                                  const Values& values);
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_METADATA_WRITER_HPP
