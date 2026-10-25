// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The splice: every edit is a replacement of one scanned range, collected,
// sorted and applied from the end of the packet backwards, so no range moves
// before it is used. A rewritten element keeps the name it was written with
// and takes the form XMP gives its property (XMP specification part 2,
// recalled): dc:title and dc:description an rdf:Alt with one x-default item,
// dc:creator an rdf:Seq and dc:subject an rdf:Bag with an item per name or
// keyword (split at semicolons, and for keywords also at commas), every other
// property its text. A removed attribute takes the white space before it.

#include "writer/metadata_writer.hpp"

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "syntax/pdf_doc_encoding.hpp"
#include "syntax/stream.hpp"
#include "writer/document_id.hpp"
#include "writer/stream_copier.hpp"

#include <algorithm>
#include <utility>

namespace pdfkit::writer {

namespace {

using syntax::Dictionary;
using syntax::MetadataReader;
using syntax::Object;
using syntax::XmpScanner;

struct Splice {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string text;
};

std::string_view trim(std::string_view text) {
  const auto space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!text.empty() && space(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && space(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string> split(std::string_view text, bool commas) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == ';' || (commas && text[i] == ',')) {
      const std::string_view item = trim(text.substr(start, i - start));
      if (!item.empty()) {
        out.emplace_back(item);
      }
      start = i + 1;
    }
  }
  if (out.empty()) {
    out.emplace_back(trim(text));
  }
  return out;
}

std::string info_text(MetadataWriter::Field field, const std::string& value) {
  if (MetadataReader::is_date(field) && !MetadataReader::pdf_date_to_iso(value)) {
    if (const auto date = MetadataReader::iso_to_pdf_date(value)) {
      return *date;
    }
  }
  return value;
}

std::string xmp_text(MetadataWriter::Field field, const std::string& value) {
  if (MetadataReader::is_date(field) && !MetadataReader::iso_date(value)) {
    if (const auto date = MetadataReader::pdf_date_to_iso(value)) {
      return *date;
    }
  }
  return value;
}

// The element written for a property of field, named qname, holding value;
// rdf is the prefix the rdf namespace is bound to where it is written.
std::string element(MetadataWriter::Field field, std::string_view ns, std::string_view local,
                    std::string_view qname, std::string_view rdf, const std::string& value) {
  const std::string open = "<" + std::string(qname) + ">";
  const std::string close = "</" + std::string(qname) + ">";
  const std::string r = rdf.empty() ? std::string() : std::string(rdf) + ":";
  const std::string text = xmp_text(field, value);
  if (ns == XmpScanner::dc && (local == "title" || local == "description")) {
    return open + "<" + r + "Alt><" + r + "li xml:lang=\"x-default\">" + XmpScanner::escape(text) +
           "</" + r + "li></" + r + "Alt>" + close;
  }
  if (ns == XmpScanner::dc && (local == "creator" || local == "subject")) {
    const std::string kind = local == "creator" ? "Seq" : "Bag";
    std::string out = open + "<" + r + kind + ">";
    for (const std::string& item : split(text, local == "subject")) {
      out += "<" + r + "li>" + XmpScanner::escape(item) + "</" + r + "li>";
    }
    return out + "</" + r + kind + ">" + close;
  }
  return open + XmpScanner::escape(text) + close;
}

// The prefix the packet binds ns to, or fallback when the new description
// declares its own.
std::string_view prefix_of(std::string_view ns) {
  if (ns == XmpScanner::dc) {
    return "dc";
  }
  if (ns == XmpScanner::pdf) {
    return "pdf";
  }
  return "xmp";
}

// The removal of property: its range, an attribute with the white space
// before it.
Splice removal(std::string_view packet, const XmpScanner::Property& property) {
  std::size_t begin = property.begin;
  if (property.form == XmpScanner::Form::attribute) {
    while (begin > 0 && (packet[begin - 1] == ' ' || packet[begin - 1] == '\t' ||
                         packet[begin - 1] == '\r' || packet[begin - 1] == '\n')) {
      --begin;
    }
  }
  return {begin, property.end, ""};
}

void set_catalog_packet(ObjectGraph& graph, const Object& entry, const Dictionary& catalog,
                        const syntax::Stream& stream, const std::string& packet) {
  const syntax::Stream rewritten =
      StreamCopier::plain(stream.dictionary(), platform::as_bytes(packet));
  if (const auto reference = entry.reference()) {
    graph.set(reference->number, Object::make_stream(rewritten));
    return;
  }
  Dictionary updated = catalog;
  updated.set("Metadata", Object::make_reference(graph.add(Object::make_stream(rewritten))));
  graph.set(graph.root(), Object::make_dictionary(std::move(updated)));
}

void write_info(ObjectGraph& graph, const MetadataWriter::Values& values) {
  if (values.empty()) {
    return;
  }
  const Object entry = graph.trailer().get("Info");
  Dictionary info;
  if (const Dictionary* existing = graph.resolve(entry).dictionary()) {
    info = *existing;
  }
  for (const auto& [field, value] : values) {
    const std::string name(MetadataReader::name(field));
    if (!value) {
      info.erase(name);
      continue;
    }
    const std::string bytes = syntax::PdfDocEncoding::text_string(info_text(field, *value));
    bool printable = true;
    for (const char c : bytes) {
      const auto code = static_cast<unsigned char>(c);
      printable = printable && code >= 0x20 && code < 0x7F;
    }
    info.set(name, Object::make_string(bytes, !printable));
  }
  if (const auto reference = entry.reference();
      reference && !graph.get(reference->number).is_null()) {
    graph.set(reference->number, Object::make_dictionary(std::move(info)));
    return;
  }
  Dictionary trailer = graph.trailer();
  trailer.set("Info", Object::make_reference(graph.add(Object::make_dictionary(std::move(info)))));
  graph.set_trailer(std::move(trailer));
}

}  // namespace

std::vector<MetadataWriter::Written> MetadataWriter::preview(const Values& values) {
  std::vector<Written> out;
  for (const auto& [field, value] : values) {
    Written written;
    written.field = field;
    written.removed = !value;
    if (value) {
      written.info = info_text(field, *value);
      written.info_encoding = std::string(syntax::PdfDocEncoding::name(
          syntax::PdfDocEncoding::decode(syntax::PdfDocEncoding::text_string(written.info)).form));
      written.xmp = xmp_text(field, *value);
    }
    out.push_back(std::move(written));
  }
  return out;
}

std::string MetadataWriter::rewrite_packet(std::string_view packet,
                                           const XmpScanner::Result& scanned,
                                           const Values& values) {
  std::vector<Splice> splices;
  for (const XmpScanner::Property& property : scanned.properties) {
    if (property.ns == XmpScanner::xmp_mm) {
      const bool identifier = property.local == "DocumentID" || property.local == "InstanceID";
      if (identifier && (property.form == XmpScanner::Form::attribute ||
                         property.form == XmpScanner::Form::simple)) {
        const std::string_view placeholder = property.local == "DocumentID"
                                                 ? DocumentId::xmp_document_placeholder
                                                 : DocumentId::xmp_instance_placeholder;
        splices.push_back({property.value_begin, property.value_end, std::string(placeholder)});
      } else {
        splices.push_back(removal(packet, property));
      }
    }
  }
  std::string added;
  std::vector<std::string_view> declared;
  for (const auto& [field, value] : values) {
    const auto pairs = MetadataReader::pairs(field);
    bool present = false;
    for (const XmpScanner::Property& property : scanned.properties) {
      bool paired = false;
      for (const MetadataReader::Pair& pair : pairs) {
        paired = paired || (property.ns == pair.ns && property.local == pair.local);
      }
      if (!paired) {
        continue;
      }
      present = true;
      if (!value) {
        splices.push_back(removal(packet, property));
      } else if (property.form == XmpScanner::Form::attribute) {
        splices.push_back({property.value_begin, property.value_end,
                           XmpScanner::escape(xmp_text(field, *value))});
      } else {
        splices.push_back({property.begin, property.end,
                           element(field, property.ns, property.local, property.qname,
                                   scanned.rdf_prefix, *value)});
      }
    }
    if (!present && value && scanned.rdf_end) {
      const MetadataReader::Pair& primary = pairs.front();
      const std::string_view prefix = prefix_of(primary.ns);
      if (std::find(declared.begin(), declared.end(), primary.ns) == declared.end()) {
        declared.push_back(primary.ns);
      }
      added += element(field, primary.ns, primary.local,
                       std::string(prefix) + ":" + std::string(primary.local), "rdf", *value);
    }
  }
  if (!added.empty()) {
    std::string description =
        "<rdf:Description xmlns:rdf=\"" + std::string(XmpScanner::rdf) + "\" rdf:about=\"\"";
    for (const std::string_view ns : declared) {
      description += " xmlns:" + std::string(prefix_of(ns)) + "=\"" + std::string(ns) + "\"";
    }
    description += ">" + added + "</rdf:Description>\n";
    splices.push_back({*scanned.rdf_end, *scanned.rdf_end, std::move(description)});
  }
  std::stable_sort(splices.begin(), splices.end(),
                   [](const Splice& a, const Splice& b) { return a.begin > b.begin; });
  std::string out(packet);
  std::size_t limit = out.size();
  for (const Splice& splice : splices) {
    // Ranges are disjoint by construction; one that overlaps an applied one
    // is a property counted twice and is skipped.
    if (splice.end > limit) {
      continue;
    }
    out.replace(splice.begin, splice.end - splice.begin, splice.text);
    limit = splice.begin;
  }
  return out;
}

void MetadataWriter::apply(ObjectGraph& graph, const Values& values,
                           std::vector<ChannelTable::Warning>& warnings) {
  write_info(graph, values);
  const std::uint32_t root = graph.root();
  const Object catalog_value = graph.get(root);
  const Dictionary* catalog = catalog_value.dictionary();
  if (catalog == nullptr || !catalog->contains("Metadata")) {
    return;
  }
  const Object entry = catalog->get("Metadata");
  const Object value = graph.resolve(entry);
  const syntax::Stream* stream = value.stream();
  std::optional<std::string> packet;
  XmpScanner::Result scanned;
  if (stream != nullptr) {
    const syntax::Filters::Result decoded = graph.document().decode(*stream);
    if (!decoded.damaged && decoded.remaining.empty()) {
      packet.emplace(reinterpret_cast<const char*>(decoded.bytes.data()), decoded.bytes.size());
      scanned = XmpScanner::scan(*packet);
    }
  }
  if (!packet || !scanned.readable) {
    Dictionary updated = *catalog;
    updated.erase("Metadata");
    graph.set(root, Object::make_dictionary(std::move(updated)));
    warnings.push_back({"xmp_unreadable",
                        "the catalog's XMP packet does not scan" +
                            (scanned.fault.empty() ? std::string() : " (" + scanned.fault + ")") +
                            ", so it is dropped from the output",
                        std::nullopt});
    return;
  }
  const std::string rewritten = rewrite_packet(*packet, scanned, values);
  if (rewritten != *packet) {
    set_catalog_packet(graph, entry, *catalog, *stream, rewritten);
  }
}

}  // namespace pdfkit::writer
