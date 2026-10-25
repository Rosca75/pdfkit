// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// XmpScanner reads an XMP packet (docs/DESIGN.md section 10) as the XML it is
// and records, for every property of every rdf:Description, its namespace,
// its name and the byte ranges it occupies, so the metadata writer can splice
// a new value in and keep every other byte. A property is written in one of
// the forms of the XMP specification part 1 (recalled): an attribute of the
// rdf:Description (the shorthand), a simple element holding text, or an
// element holding one rdf:Alt, rdf:Seq or rdf:Bag of rdf:li items; anything
// else (a structure, an rdf:resource, nested elements) is recorded as a
// structure with no value. Names are resolved through the namespace
// declarations in scope, so a packet that binds the XMP namespace to the old
// prefix "xap" is read alike. The scanner never guesses: a tag that does not
// close, an end tag that names another element, an unterminated quote,
// comment or section, an element left open, a packet in UTF-16 or one with
// no rdf:RDF is unreadable, and the reason is named; an empty packet holds no
// property.

#ifndef PDFKIT_SYNTAX_XMP_SCANNER_HPP
#define PDFKIT_SYNTAX_XMP_SCANNER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::syntax {

class XmpScanner {
 public:
  // The namespaces the metadata functions name (XMP specification part 1
  // and part 2, recalled; ISO 32000-1 section 14.3.2 names the packet).
  static constexpr std::string_view rdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
  static constexpr std::string_view dc = "http://purl.org/dc/elements/1.1/";
  static constexpr std::string_view xmp = "http://ns.adobe.com/xap/1.0/";
  static constexpr std::string_view pdf = "http://ns.adobe.com/pdf/1.3/";
  static constexpr std::string_view xmp_mm = "http://ns.adobe.com/xap/1.0/mm/";
  static constexpr std::string_view xml = "http://www.w3.org/XML/1998/namespace";

  enum class Form : std::uint8_t { attribute, simple, alt, seq, bag, structure };

  // One rdf:li of an array: the range of its character data and that text
  // with the entities read.
  struct Item {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string text;
    std::string lang;
  };

  struct Property {
    std::string ns;
    std::string local;
    // The name as written, with its prefix.
    std::string qname;
    Form form = Form::simple;
    // The whole property: an element from its "<" to past its end tag's
    // ">", an attribute from its name to past its closing quote.
    std::size_t begin = 0;
    std::size_t end = 0;
    // The value's range: inside the quotes of an attribute, between the tags
    // of an element.
    std::size_t value_begin = 0;
    std::size_t value_end = 0;
    std::vector<Item> items;
    // The value read: an attribute's or a simple element's text, an
    // rdf:Alt's x-default item (else its first), an rdf:Seq's or rdf:Bag's
    // items joined with "; "; empty for a structure.
    std::string value;
  };

  struct Result {
    bool readable = true;
    // Why the packet is unreadable.
    std::string fault;
    std::vector<Property> properties;
    // Where the rdf:RDF element's end tag starts, and the prefix its name is
    // written with, so a new rdf:Description can be added before it.
    std::optional<std::size_t> rdf_end;
    std::string rdf_prefix;

    // The first property of namespace ns named local, or none.
    [[nodiscard]] const Property* find(std::string_view ns, std::string_view local) const;
  };

  [[nodiscard]] static Result scan(std::string_view packet);

  // text with the five predefined entities and the character references
  // read; an unknown entity is kept as written.
  [[nodiscard]] static std::string unescape(std::string_view text);
  // text with "&", "<", ">", '"' and "'" written as entities.
  [[nodiscard]] static std::string escape(std::string_view text);
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_XMP_SCANNER_HPP
