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
// property its text. A removed attribute takes the white space before it. A
// value "repeats" an original when the original, folded as the self-check
// folds a search string by default, occurs in it folded alike.

#include "writer/metadata_writer.hpp"

#include "platform/byte_span.hpp"
#include "platform/unicode/folding.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "syntax/pdf_doc_encoding.hpp"
#include "syntax/stream.hpp"
#include "writer/document_id.hpp"
#include "writer/stream_copier.hpp"

#include <algorithm>
#include <span>
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

// The content of the element written for a property of field holding value;
// rdf is the prefix the rdf namespace is bound to where it is written.
std::string content(MetadataWriter::Field field, std::string_view ns, std::string_view local,
                    std::string_view rdf, const std::string& value) {
  const std::string r = rdf.empty() ? std::string() : std::string(rdf) + ":";
  const std::string text = xmp_text(field, value);
  if (ns == XmpScanner::dc && (local == "title" || local == "description")) {
    return "<" + r + "Alt><" + r + "li xml:lang=\"x-default\">" + XmpScanner::escape(text) + "</" +
           r + "li></" + r + "Alt>";
  }
  if (ns == XmpScanner::dc && (local == "creator" || local == "subject")) {
    const std::string kind = local == "creator" ? "Seq" : "Bag";
    std::string out = "<" + r + kind + ">";
    for (const std::string& item : split(text, local == "subject")) {
      out += "<" + r + "li>" + XmpScanner::escape(item) + "</" + r + "li>";
    }
    return out + "</" + r + kind + ">";
  }
  return XmpScanner::escape(text);
}

// The element written for a new property named qname.
std::string element(MetadataWriter::Field field, std::string_view ns, std::string_view local,
                    std::string_view qname, std::string_view rdf, const std::string& value) {
  return "<" + std::string(qname) + ">" + content(field, ns, local, rdf, value) + "</" +
         std::string(qname) + ">";
}

// The rewrite of an element property: its content replaced, its start tag (a
// namespace it declares included) and end tag kept; an empty element written
// as a start tag, the content and an end tag.
Splice rewrite_element(std::string_view packet, const XmpScanner::Property& property,
                       const std::string& inner) {
  const bool empty = property.end >= 2 && packet.substr(property.end - 2, 2) == "/>";
  if (!empty) {
    return {property.value_begin, property.value_end, inner};
  }
  std::string_view start = packet.substr(property.begin, property.end - 2 - property.begin);
  while (!start.empty() && (start.back() == ' ' || start.back() == '\t' || start.back() == '\r' ||
                            start.back() == '\n')) {
    start.remove_suffix(1);
  }
  return {property.begin, property.end,
          std::string(start) + ">" + inner + "</" + property.qname + ">"};
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

// The originals folded as the self-check folds a search string by default.
std::vector<std::u32string> fold_all(std::span<const std::string> originals) {
  std::vector<std::u32string> out;
  for (const std::string& original : originals) {
    std::u32string folded =
        platform::unicode::Folding::fold(platform::unicode::Folding::decode(original), {}).text;
    if (!trim(platform::unicode::Folding::encode(folded)).empty()) {
      out.push_back(std::move(folded));
    }
  }
  return out;
}

bool holds(std::string_view text, const std::vector<std::u32string>& originals) {
  if (originals.empty()) {
    return false;
  }
  const std::u32string folded =
      platform::unicode::Folding::fold(platform::unicode::Folding::decode(text), {}).text;
  return std::ranges::any_of(originals, [&folded](const std::u32string& original) {
    return folded.find(original) != std::u32string::npos;
  });
}

// The text strings of value, those of its arrays and dictionaries included.
void strings_of(ObjectGraph& graph, const Object& value, int depth, std::string& out) {
  if (depth > 32) {
    return;
  }
  const Object resolved = graph.resolve(value);
  if (const Object::String* string = resolved.string()) {
    out += syntax::PdfDocEncoding::decode(string->bytes).text;
    out += '\n';
  } else if (const Object::Array* items = resolved.array()) {
    for (const Object& item : *items) {
      strings_of(graph, item, depth + 1, out);
    }
  } else if (const Dictionary* dictionary = resolved.dictionary()) {
    for (const auto& [key, item] : dictionary->entries()) {
      strings_of(graph, item, depth + 1, out);
    }
  }
}

// The text of an XMP property: its value, or for a structure the character
// data of its range.
std::string property_text(std::string_view packet, const XmpScanner::Property& property) {
  if (property.form != XmpScanner::Form::structure) {
    std::string text = property.value;
    for (const XmpScanner::Item& item : property.items) {
      text += '\n' + item.text;
    }
    return text;
  }
  std::string text;
  bool in_tag = false;
  for (std::size_t i = property.begin; i < property.end && i < packet.size(); ++i) {
    const char c = packet[i];
    if (c == '<') {
      in_tag = true;
      text += '\n';
    } else if (c == '>') {
      in_tag = false;
    } else if (!in_tag) {
      text += c;
    }
  }
  return XmpScanner::unescape(text);
}

bool paired_with_a_field(const XmpScanner::Property& property) {
  return std::ranges::any_of(MetadataReader::fields, [&property](MetadataWriter::Field field) {
    return std::ranges::any_of(MetadataReader::pairs(field), [&property](const auto& pair) {
      return property.ns == pair.ns && property.local == pair.local;
    });
  });
}

void write_info(ObjectGraph& graph, const MetadataWriter::Values& values,
                const std::vector<std::u32string>& originals) {
  if (values.empty() && originals.empty()) {
    return;
  }
  const Object entry = graph.trailer().get("Info");
  Dictionary info;
  if (const Dictionary* existing = graph.resolve(entry).dictionary()) {
    info = *existing;
  }
  // An entry outside the field list that repeats a redacted string goes,
  // because nothing else can rewrite it.
  std::vector<std::string> repeats;
  for (const auto& [key, item] : info.entries()) {
    if (MetadataReader::field(key)) {
      continue;
    }
    std::string text;
    strings_of(graph, item, 0, text);
    if (holds(text, originals)) {
      repeats.push_back(key);
    }
  }
  if (values.empty() && repeats.empty()) {
    return;
  }
  for (const std::string& key : repeats) {
    info.erase(key);
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
                                           const XmpScanner::Result& scanned, const Values& values,
                                           std::span<const std::string> originals) {
  const std::vector<std::u32string> folded = fold_all(originals);
  std::vector<Splice> splices;
  for (const XmpScanner::Property& other : scanned.others) {
    // The toolkit attribute often names the producer.
    if (holds(other.value, folded)) {
      splices.push_back(removal(packet, other));
    }
  }
  for (const XmpScanner::Property& property : scanned.properties) {
    if (property.ns != XmpScanner::xmp_mm && !paired_with_a_field(property) &&
        holds(property_text(packet, property), folded)) {
      // A property no field pairs with that repeats a redacted string goes,
      // because nothing else can rewrite it.
      splices.push_back(removal(packet, property));
      continue;
    }
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
        splices.push_back(rewrite_element(
            packet, property,
            content(field, property.ns, property.local, scanned.rdf_prefix, *value)));
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
                           std::span<const std::string> originals,
                           std::vector<ChannelTable::Warning>& warnings) {
  write_info(graph, values, fold_all(originals));
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
  if (packet && scanned.readable && scanned.properties.empty() && !scanned.rdf_end) {
    // A packet that states nothing (empty, or an empty rdf:RDF) has nothing
    // to rewrite and no place to add to; it goes, as no packet at all.
    Dictionary updated = *catalog;
    updated.erase("Metadata");
    graph.set(root, Object::make_dictionary(std::move(updated)));
    return;
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
  const std::string rewritten = rewrite_packet(*packet, scanned, values, originals);
  if (rewritten != *packet) {
    set_catalog_packet(graph, entry, *catalog, *stream, rewritten);
  }
}

}  // namespace pdfkit::writer
