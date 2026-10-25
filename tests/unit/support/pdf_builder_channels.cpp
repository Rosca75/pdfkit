// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The planted fixtures. Every document draws one line of ordinary text in
// Helvetica; the marker goes into the channel through the builder's objects,
// written as literal strings so the bytes of the marker appear as they are.

#include "support/pdf_builder_channels.hpp"

#include "support/pdf_builder.hpp"
#include "support/pdf_encryptor.hpp"

#include <array>
#include <cstdio>
#include <stdexcept>

namespace pdfkit::test {

namespace {

std::string ref(std::uint32_t number) {
  return std::to_string(number) + " 0 R";
}

std::string literal(const std::string& text) {
  return "(" + text + ")";
}

const std::string page_content = "BT /F1 12 Tf 72 720 Td (" +
                                 std::string(PdfBuilderChannels::drawn_text) + ") Tj ET";

std::string xmp(const std::string& description, const std::string& history) {
  std::string packet =
      "<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d'?>\n"
      "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF "
      "xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>\n"
      "<rdf:Description rdf:about='' xmlns:dc='http://purl.org/dc/elements/1.1/' "
      "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
      "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#' "
      "xmpMM:DocumentID='uuid:1234' xmpMM:InstanceID=\"uuid:5678\">\n"
      "<dc:title><rdf:Alt><rdf:li xml:lang='x-default'>" +
      description + "</rdf:li></rdf:Alt></dc:title>\n";
  if (!history.empty()) {
    packet +=
        "<xmpMM:History><rdf:Seq><rdf:li rdf:parseType='Resource'><stEvt:action>saved"
        "</stEvt:action><stEvt:softwareAgent>" +
        history + "</stEvt:softwareAgent></rdf:li></rdf:Seq></xmpMM:History>\n";
  }
  packet += "</rdf:Description></rdf:RDF></x:xmpmeta>\n<?xpacket end='w'?>";
  return packet;
}

// One page, its font, and room for the knobs.
struct Skeleton {
  PdfBuilder builder;
  std::uint32_t font = 0;
  std::uint32_t page = 0;
  std::uint32_t contents = 0;
  std::string page_extra;
  std::string catalog_extra;
  std::string content = page_content;
  std::string resources_extra;
  bool flate = true;

  Skeleton() {
    builder.begin_document();
    font = builder.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    page = builder.reserve();
    contents = builder.reserve();
  }

  std::string build(const PdfBuilder::Layout& layout = {}) {
    write_objects();
    return builder.build(layout);
  }

  void write_objects() {
    builder.set_stream(contents, "", content, StreamOptions{flate});
    builder.set(page, "<< /Type /Page /Parent " + ref(builder.page_tree()) +
                          " /MediaBox [0 0 612 792] /Resources << /Font << /F1 " + ref(font) +
                          " >> " + resources_extra + " >> /Contents " + ref(contents) + " " +
                          page_extra + " >>");
    builder.set(builder.page_tree(),
                "<< /Type /Pages /Kids [" + ref(page) + "] /Count 1 >>");
    builder.set(builder.catalog(), "<< /Type /Catalog /Pages " + ref(builder.page_tree()) + " " +
                                       catalog_extra + " >>");
  }
};

std::string plant(std::string_view id, const std::string& marker) {
  Skeleton s;
  PdfBuilder& b = s.builder;
  if (id == "ch_revisions") {
    s.flate = false;
    s.content = "BT /F1 12 Tf 72 720 Td (" + marker + ") Tj ET";
    s.write_objects();
    b.new_revision();
    b.set_stream(s.contents, "", page_content, StreamOptions{true});
    return b.build();
  }
  if (id == "ch_unreferenced") {
    (void)b.add_stream("/Orphan true", "BT /F1 12 Tf 72 600 Td (" + marker + ") Tj ET");
  } else if (id == "ch_thumbnail") {
    const std::uint32_t thumb = b.add_stream(
        "/Width " + std::to_string(marker.size()) +
            " /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8",
        marker);
    s.page_extra = "/Thumb " + ref(thumb);
  } else if (id == "ch_pieceinfo") {
    s.page_extra = "/PieceInfo << /App << /LastModified (D:20260101) /Private " +
                   literal(marker) + " >> >>";
  } else if (id == "ch_last_modified") {
    s.page_extra = "/LastModified " + literal(marker);
  } else if (id == "ch_object_metadata") {
    const std::uint32_t metadata =
        b.add_stream("/Type /Metadata /Subtype /XML", xmp(marker, ""));
    s.page_extra = "/Metadata " + ref(metadata);
  } else if (id == "ch_javascript") {
    s.catalog_extra = "/OpenAction << /S /JavaScript /JS " + literal("app.alert('" + marker + "');") +
                      " >>";
  } else if (id == "ch_embedded_file") {
    const std::uint32_t data =
        b.add_stream("/Type /EmbeddedFile", "attachment body " + marker);
    const std::uint32_t spec = b.add("<< /Type /Filespec /F " + literal(marker + ".txt") +
                                     " /EF << /F " + ref(data) + " >> >>");
    s.catalog_extra = "/Names << /EmbeddedFiles << /Names [(a) " + ref(spec) + "] >> >>";
  } else if (id == "ch_annotation") {
    const std::uint32_t appearance = b.add_stream(
        "/Type /XObject /Subtype /Form /BBox [0 0 100 20] /Resources << /Font << /F1 " +
            ref(s.font) + " >> >>",
        "BT /F1 8 Tf 2 6 Td (" + marker + ") Tj ET");
    const std::uint32_t annotation =
        b.add("<< /Type /Annot /Subtype /FreeText /Rect [400 600 500 620] /DA (/Helv 8 Tf) "
              "/Contents " +
              literal(marker) + " /AP << /N " + ref(appearance) + " >> >>");
    s.page_extra = "/Annots [" + ref(annotation) + "]";
  } else if (id == "ch_acroform") {
    const std::uint32_t field = b.add("<< /FT /Tx /T (name) /V " + literal(marker) + " >>");
    s.catalog_extra = "/AcroForm << /Fields [" + ref(field) + "] /NeedAppearances true >>";
  } else if (id == "ch_outline") {
    const std::uint32_t outlines = b.reserve();
    const std::uint32_t item = b.add("<< /Title " + literal(marker) + " /Parent " +
                                     ref(outlines) + " >>");
    b.set(outlines, "<< /Type /Outlines /First " + ref(item) + " /Last " + ref(item) +
                        " /Count 1 >>");
    s.catalog_extra = "/Outlines " + ref(outlines);
  } else if (id == "ch_named_dest") {
    s.catalog_extra = "/Dests << /" + marker + " [" + ref(s.page) + " /Fit] >>";
  } else if (id == "ch_struct_tree") {
    const std::uint32_t root = b.reserve();
    const std::uint32_t element = b.add("<< /Type /StructElem /S /P /P " + ref(root) +
                                        " /Pg " + ref(s.page) + " /Alt " + literal(marker) +
                                        " >>");
    b.set(root, "<< /Type /StructTreeRoot /K [" + ref(element) + "] >>");
    s.catalog_extra = "/StructTreeRoot " + ref(root) + " /MarkInfo << /Marked true >>";
    s.page_extra = "/StructParents 0";
  } else if (id == "ch_optional_content") {
    const std::uint32_t group = b.add("<< /Type /OCG /Name " + literal(marker) + " >>");
    s.resources_extra = "/Properties << /oc1 " + ref(group) + " >>";
    s.content = "/OC /oc1 BDC " + page_content + " EMC";
    s.catalog_extra = "/OCProperties << /OCGs [" + ref(group) + "] /D << /Name " +
                      literal(marker) + " /ON [" + ref(group) + "] /Order [" + ref(group) +
                      "] >> >>";
  } else if (id == "ch_page_labels") {
    s.catalog_extra = "/PageLabels << /Nums [0 << /S /D /P " + literal(marker) + " >>] >>";
  } else if (id == "ch_xmp_history") {
    const std::uint32_t metadata =
        b.add_stream("/Type /Metadata /Subtype /XML", xmp("An ordinary title", marker));
    s.catalog_extra = "/Metadata " + ref(metadata);
  } else if (id == "ch_threads") {
    const std::uint32_t thread = b.reserve();
    const std::uint32_t bead = b.reserve();
    b.set(bead, "<< /Type /Bead /T " + ref(thread) + " /N " + ref(bead) + " /V " + ref(bead) +
                    " /P " + ref(s.page) + " /R [72 700 300 740] >>");
    b.set(thread, "<< /Type /Thread /F " + ref(bead) + " /I << /Title " + literal(marker) +
                      " >> >>");
    s.catalog_extra = "/Threads [" + ref(thread) + "]";
  } else if (id == "ch_web_capture") {
    s.catalog_extra = "/SpiderInfo << /V 1.0 /C [<< /URL " + literal(marker) + " >>] >>";
  } else if (id == "ch_portfolio") {
    s.catalog_extra = "/Collection << /Type /Collection /D " + literal(marker) + " >>";
  } else if (id == "ch_perms_legal") {
    s.catalog_extra = "/Perms << /DocMDP << /Type /Sig /Reason " + literal(marker) + " >> >>";
  } else if (id == "ch_encrypt") {
    PdfEncryptor::Spec spec;
    spec.extra_entries = "/Vendor " + literal(marker);
    b.encrypt(spec);
  } else if (id == "ch_uri_base") {
    s.catalog_extra = "/URI << /Base " + literal(marker) + " >>";
  } else if (id == "ch_search_index") {
    s.catalog_extra = "/SearchIndex << /Data " + literal(marker) + " >>";
  } else {
    throw std::logic_error("PdfBuilderChannels: no knob for the row " + std::string(id));
  }
  return s.build();
}

}  // namespace

const std::vector<std::string_view>& PdfBuilderChannels::rows() {
  static const std::vector<std::string_view> ids{
      "ch_revisions",     "ch_unreferenced",     "ch_thumbnail",    "ch_pieceinfo",
      "ch_last_modified", "ch_object_metadata",  "ch_javascript",   "ch_embedded_file",
      "ch_annotation",    "ch_acroform",         "ch_outline",      "ch_named_dest",
      "ch_struct_tree",   "ch_optional_content", "ch_page_labels",  "ch_xmp_history",
      "ch_threads",       "ch_web_capture",      "ch_portfolio",    "ch_perms_legal",
      "ch_encrypt",       "ch_uri_base",         "ch_search_index"};
  return ids;
}

std::string PdfBuilderChannels::marker(int row, int n) {
  std::array<char, 32> text{};
  (void)std::snprintf(text.data(), text.size(), "PDFKITMARK%02dX%02d", row, n);
  return text.data();
}

std::string PdfBuilderChannels::planted(std::string_view id, const std::string& marker) {
  return plant(id, marker);
}

std::string PdfBuilderChannels::leak_fixture(std::uint32_t pages) {
  PdfBuilder b;
  b.begin_document();
  const std::uint32_t font = b.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  const std::uint32_t struct_root = b.reserve();
  std::vector<std::uint32_t> page_numbers;
  std::vector<std::uint32_t> contents;
  std::vector<std::uint32_t> groups;
  std::vector<std::uint32_t> elements;
  std::string xobjects;
  std::string properties;
  std::string labels;
  for (std::uint32_t n = 1; n <= pages; ++n) {
    const int i = static_cast<int>(n);
    page_numbers.push_back(b.reserve());
    contents.push_back(b.reserve());
    const std::uint32_t form = b.add_stream(
        "/Type /XObject /Subtype /Form /BBox [0 0 612 792] /Resources << /Font << /F1 " +
            ref(font) + " >> >>",
        "BT /F1 10 Tf 72 650 Td (" + marker(91, i) + ") Tj ET", StreamOptions{true});
    const std::string image_data = marker(92, i);
    const std::uint32_t image =
        b.add_stream("/Type /XObject /Subtype /Image /Width " + std::to_string(image_data.size()) +
                         " /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8",
                     image_data, StreamOptions{true});
    // A form without resources of its own: it reads the inherited dictionary.
    const std::uint32_t shared = b.add_stream(
        "/Type /XObject /Subtype /Form /BBox [0 0 612 792]",
        "BT /F1 10 Tf 72 630 Td (" + marker(93, i) + ") Tj ET", StreamOptions{true});
    groups.push_back(b.add("<< /Type /OCG /Name " + literal(marker(14, i)) + " >>"));
    xobjects += "/Fm" + std::to_string(n) + " " + ref(form) + " /Im" + std::to_string(n) + " " +
                ref(image) + " /Xs" + std::to_string(n) + " " + ref(shared) + " ";
    properties += "/oc" + std::to_string(n) + " " + ref(groups.back()) + " ";
    labels += std::to_string(n - 1) + " << /S /D /P " + literal(marker(15, i)) + " >> ";
    elements.push_back(b.add("<< /Type /StructElem /S /P /P " + ref(struct_root) + " /Pg " +
                             ref(page_numbers.back()) + " /ActualText " +
                             literal(marker(13, i)) + " >>"));
    (void)b.add("<< /Orphan " + literal(marker(2, i)) + " >>");
    const std::uint32_t thumb = b.add_stream(
        "/Width " + std::to_string(marker(3, i).size()) +
            " /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8",
        marker(3, i));
    const std::uint32_t annotation = b.reserve();
    const std::uint32_t popup = b.add("<< /Type /Annot /Subtype /Popup /Rect [400 500 500 600] "
                                      "/Parent " +
                                      ref(annotation) + " /Contents " + literal(marker(9, i)) +
                                      " >>");
    b.set(annotation, "<< /Type /Annot /Subtype /Text /Rect [400 600 420 620] /Contents " +
                          literal(marker(9, i)) + " /Popup " + ref(popup) + " >>");
    // The first revision draws a marker the later revision removes.
    b.set_stream(contents.back(), "",
                 "BT /F1 12 Tf 72 720 Td (" + marker(1, i) + ") Tj ET", StreamOptions{true});
    b.set(page_numbers.back(), "<< /Type /Page /Parent " + ref(b.page_tree()) +
                                   " /MediaBox [0 0 612 792] /Contents " + ref(contents.back()) +
                                   " /Thumb " + ref(thumb) + " /Annots [" + ref(annotation) +
                                   "] /StructParents " + std::to_string(n - 1) + " >>");
  }
  std::string kids;
  std::string all_groups;
  std::string all_elements;
  for (std::size_t k = 0; k < page_numbers.size(); ++k) {
    kids += ref(page_numbers[k]) + " ";
    all_groups += ref(groups[k]) + " ";
    all_elements += ref(elements[k]) + " ";
  }
  b.set(b.page_tree(), "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) +
                           " /Resources << /Font << /F1 " + ref(font) + " >> /XObject << " +
                           xobjects + ">> /Properties << " + properties + ">> >> >>");
  b.set(struct_root, "<< /Type /StructTreeRoot /K [" + all_elements + "] >>");
  b.set(b.catalog(), "<< /Type /Catalog /Pages " + ref(b.page_tree()) + " /StructTreeRoot " +
                         ref(struct_root) + " /MarkInfo << /Marked true >> /PageLabels << /Nums [" +
                         labels + "] >> /OCProperties << /OCGs [" + all_groups + "] /D << /ON [" +
                         all_groups + "] /Order [" + all_groups + "] >> >> >>");
  b.new_revision();
  for (std::uint32_t n = 1; n <= pages; ++n) {
    const std::string s = std::to_string(n);
    b.set_stream(contents[n - 1], "",
                 "/OC /oc" + s + " BDC BT /F1 12 Tf 72 720 Td (" + marker(90, static_cast<int>(n)) +
                     ") Tj ET EMC q /Fm" + s + " Do Q q 100 0 0 10 72 600 cm /Im" + s +
                     " Do Q q /Xs" + s + " Do Q",
                 StreamOptions{true});
  }
  return b.build();
}

}  // namespace pdfkit::test
