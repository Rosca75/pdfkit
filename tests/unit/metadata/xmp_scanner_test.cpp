// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of syntax::XmpScanner: each property form (attribute, simple
// element, rdf:Alt, rdf:Seq, rdf:Bag, structure) is found with byte ranges
// that cut exactly its value out of the packet; prefixes are resolved through
// the declarations in scope; a custom namespace is listed and left as it is;
// each malformation is unreadable with its reason.

#include "syntax/xmp_scanner.hpp"

#include "metadata/support/metadata_fixture.hpp"

#include <doctest.h>

#include <string>
#include <string_view>

namespace {

using pdfkit::syntax::XmpScanner;
using pdfkit::test::MetadataFixture;

std::string slice(const std::string& packet, std::size_t begin, std::size_t end) {
  REQUIRE(begin <= end);
  REQUIRE(end <= packet.size());
  return packet.substr(begin, end - begin);
}

}  // namespace

TEST_CASE("each property form is found with its exact byte ranges") {
  const std::string packet = MetadataFixture::xmp(
      "<dc:title><rdf:Alt><rdf:li xml:lang=\"de\">Titel</rdf:li>"
      "<rdf:li xml:lang=\"x-default\">The &amp; title</rdf:li></rdf:Alt></dc:title>\n"
      "<dc:creator><rdf:Seq><rdf:li>Ann</rdf:li><rdf:li>Bob</rdf:li></rdf:Seq></dc:creator>\n"
      "<dc:subject><rdf:Bag><rdf:li>one</rdf:li><rdf:li>two</rdf:li></rdf:Bag></dc:subject>\n"
      "<xmp:CreatorTool>Writer &lt;7&gt;</xmp:CreatorTool>\n",
      "pdf:Producer='Maker 1.0'");
  const XmpScanner::Result result = XmpScanner::scan(packet);
  REQUIRE(result.readable);

  const XmpScanner::Property* producer = result.find(XmpScanner::pdf, "Producer");
  REQUIRE(producer != nullptr);
  CHECK(producer->form == XmpScanner::Form::attribute);
  CHECK(producer->value == "Maker 1.0");
  CHECK(slice(packet, producer->value_begin, producer->value_end) == "Maker 1.0");
  CHECK(slice(packet, producer->begin, producer->end) == "pdf:Producer='Maker 1.0'");

  const XmpScanner::Property* title = result.find(XmpScanner::dc, "title");
  REQUIRE(title != nullptr);
  CHECK(title->form == XmpScanner::Form::alt);
  CHECK(title->value == "The & title");
  REQUIRE(title->items.size() == 2);
  CHECK(slice(packet, title->items[1].begin, title->items[1].end) == "The &amp; title");
  CHECK(title->items[0].lang == "de");
  const std::string whole = slice(packet, title->begin, title->end);
  CHECK(whole.starts_with("<dc:title>"));
  CHECK(whole.ends_with("</dc:title>"));

  const XmpScanner::Property* creator = result.find(XmpScanner::dc, "creator");
  REQUIRE(creator != nullptr);
  CHECK(creator->form == XmpScanner::Form::seq);
  CHECK(creator->value == "Ann; Bob");

  const XmpScanner::Property* subject = result.find(XmpScanner::dc, "subject");
  REQUIRE(subject != nullptr);
  CHECK(subject->form == XmpScanner::Form::bag);
  CHECK(subject->value == "one; two");

  const XmpScanner::Property* tool = result.find(XmpScanner::xmp, "CreatorTool");
  REQUIRE(tool != nullptr);
  CHECK(tool->form == XmpScanner::Form::simple);
  CHECK(tool->value == "Writer <7>");
  CHECK(slice(packet, tool->value_begin, tool->value_end) == "Writer &lt;7&gt;");

  REQUIRE(result.rdf_end.has_value());
  CHECK(packet.compare(*result.rdf_end, 10, "</rdf:RDF>") == 0);
  CHECK(result.rdf_prefix == "rdf");
}

TEST_CASE("names resolve through the namespace declarations in scope") {
  // The old XMP prefix "xap" names the same namespace as "xmp".
  const std::string packet =
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><r:RDF "
      "xmlns:r='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
      "<r:Description r:about='' xmlns:xap='http://ns.adobe.com/xap/1.0/'>"
      "<xap:CreateDate>2020-01-02T03:04:05Z</xap:CreateDate>"
      "<xmp:CreatorTool xmlns:xmp='urn:not-xmp'>Elsewhere</xmp:CreatorTool>"
      "</r:Description></r:RDF></x:xmpmeta>";
  const XmpScanner::Result result = XmpScanner::scan(packet);
  REQUIRE(result.readable);
  const XmpScanner::Property* created = result.find(XmpScanner::xmp, "CreateDate");
  REQUIRE(created != nullptr);
  CHECK(created->qname == "xap:CreateDate");
  CHECK(created->value == "2020-01-02T03:04:05Z");
  CHECK(result.find(XmpScanner::xmp, "CreatorTool") == nullptr);
  CHECK(result.rdf_prefix == "r");
}

TEST_CASE("a custom namespace and a structure are listed and left as written") {
  const std::string packet = MetadataFixture::xmp(
      "<my:Note xmlns:my='urn:example:custom'>Keep me</my:Note>\n"
      "<xmpMM:History><rdf:Seq><rdf:li rdf:parseType='Resource'><stEvt:action "
      "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#'>saved</stEvt:action>"
      "</rdf:li></rdf:Seq></xmpMM:History>\n");
  const XmpScanner::Result result = XmpScanner::scan(packet);
  REQUIRE(result.readable);
  const XmpScanner::Property* note = result.find("urn:example:custom", "Note");
  REQUIRE(note != nullptr);
  CHECK(note->value == "Keep me");
  const XmpScanner::Property* history = result.find(XmpScanner::xmp_mm, "History");
  REQUIRE(history != nullptr);
  CHECK(history->form == XmpScanner::Form::structure);
  CHECK(history->value.empty());
  CHECK(slice(packet, history->begin, history->end).ends_with("</xmpMM:History>"));
}

TEST_CASE("an empty element and an empty packet") {
  const XmpScanner::Result empty = XmpScanner::scan("  \n ");
  CHECK(empty.readable);
  CHECK(empty.properties.empty());
  const std::string packet = MetadataFixture::xmp("<pdf:Keywords/>\n");
  const XmpScanner::Result result = XmpScanner::scan(packet);
  REQUIRE(result.readable);
  const XmpScanner::Property* keywords = result.find(XmpScanner::pdf, "Keywords");
  REQUIRE(keywords != nullptr);
  CHECK(keywords->value.empty());
  CHECK(slice(packet, keywords->begin, keywords->end) == "<pdf:Keywords/>");
}

TEST_CASE("a malformed packet is unreadable with its reason") {
  const std::string good = MetadataFixture::xmp("<pdf:Producer>Maker</pdf:Producer>\n");
  CHECK(XmpScanner::scan(good).readable);
  const auto fault_of = [](const std::string& packet) {
    const XmpScanner::Result result = XmpScanner::scan(packet);
    CHECK_FALSE(result.readable);
    return result.fault;
  };
  CHECK(fault_of(MetadataFixture::xmp("<pdf:Producer>Maker</pdf:Creator>\n"))
            .find("closes <pdf:Producer>") != std::string::npos);
  CHECK(fault_of(MetadataFixture::xmp("<pdf:Producer>Maker\n")).find("pdf:Producer") !=
        std::string::npos);
  CHECK(fault_of(MetadataFixture::xmp("", "pdf:Producer='Maker")).find("closing quote") !=
        std::string::npos);
  CHECK(fault_of(MetadataFixture::xmp("<!-- open comment\n")).find("comment") != std::string::npos);
  CHECK(fault_of("<x:xmpmeta xmlns:x='adobe:ns:meta/'></x:xmpmeta>").find("rdf:RDF") !=
        std::string::npos);
  CHECK(fault_of(std::string("\xFE\xFF\0<", 4)).find("UTF-8") != std::string::npos);
  CHECK(fault_of(MetadataFixture::xmp("<pdf:Producer a=b>x</pdf:Producer>\n"))
            .find("no quoted value") != std::string::npos);
}

TEST_CASE("entities read and written") {
  CHECK(XmpScanner::unescape("a &lt;b&gt; &amp; &quot;c&apos; &#65;&#x42; &unknown;") ==
        "a <b> & \"c' AB &unknown;");
  CHECK(XmpScanner::escape("<a & 'b'> \"") == "&lt;a &amp; &apos;b&apos;&gt; &quot;");
}
