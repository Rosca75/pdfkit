// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The rows of the channel table, their detection, and the one rewrite pass
// that strips them. The catalog and page keys of ISO 32000-1 (sections 7.7.2
// and 7.7.3.3, tables 28 and 30, read in the design record) name what each
// row removes; the allow-lists after the rows are the author decision of
// docs/DESIGN.md section 15. Every walk here is iterative with a visited set
// and a bound, so a hostile tree cannot loop or exhaust the stack. Optional
// content goes whole: /OCProperties and every /OC key, once
// redact::OptionalContentRemover has removed the content the default
// configuration turns off, because a group's name can name things and the
// switched-off content is read by every extractor. The XMP packet
// loses its xmpMM (media management) properties, which hold the history of
// every save and the identifiers of the original (XMP specification part 2,
// section 3.3, recalled); every other byte of the packet is kept. The one
// exception is an xmpMM:DocumentID or xmpMM:InstanceID whose value is the
// placeholder the metadata writer put there, which the file writer patches
// with the output's own identifier (writer::DocumentId).

#include "writer/channel_table.hpp"

#include "syntax/document.hpp"
#include "syntax/filters.hpp"
#include "syntax/parser.hpp"
#include "syntax/stream.hpp"
#include "writer/document_id.hpp"
#include "writer/reachability.hpp"
#include "writer/stream_copier.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <utility>

namespace pdfkit::writer {

namespace {

using syntax::Dictionary;
using syntax::Object;
using Scan = ChannelTable::Scan;
using Edits = ChannelTable::Edits;

// The most nodes any tree walk of this file visits.
constexpr std::size_t walk_bound = 1000000;

// ISO 32000-1 table 28 (read): the catalog keys kept; DESIGN.md section 15.
constexpr std::array<std::string_view, 10> catalog_keys{
    "Type",     "Pages",      "Version", "OutputIntents", "ViewerPreferences",
    "PageMode", "PageLayout", "Lang",    "Metadata",      "Extensions"};
// ISO 32000-1 table 30 (read): the page keys kept.
constexpr std::array<std::string_view, 16> page_keys{
    "Type",   "Parent",   "Resources", "MediaBox", "CropBox", "BleedBox",     "TrimBox", "ArtBox",
    "Rotate", "Contents", "Group",     "UserUnit", "VP",      "BoxColorInfo", "Trans",   "Dur"};
// ISO 32000-1 table 150 (read), with /Enforce of ISO 32000-2: the standard
// viewer preferences.
constexpr std::array<std::string_view, 18> viewer_keys{"HideToolbar",
                                                       "HideMenubar",
                                                       "HideWindowUI",
                                                       "FitWindow",
                                                       "CenterWindow",
                                                       "DisplayDocTitle",
                                                       "NonFullScreenPageMode",
                                                       "Direction",
                                                       "ViewArea",
                                                       "ViewClip",
                                                       "PrintArea",
                                                       "PrintClip",
                                                       "PrintScaling",
                                                       "Duplex",
                                                       "PickTrayByPDFSize",
                                                       "PrintPageRange",
                                                       "NumCopies",
                                                       "Enforce"};
// ISO 32000-1 table 28, /PageMode (read): the standard modes.
constexpr std::array<std::string_view, 6> page_modes{"UseNone",    "UseOutlines", "UseThumbs",
                                                     "FullScreen", "UseOC",       "UseAttachments"};

// ISO 32000-1 tables 5 and 95 (read): the keys of a form XObject kept. The
// external-file keys (/F, /FFilter, /FDecodeParms), /Ref and /OPI name files
// outside the document and are dropped with every private key, because a
// producer's own key on a form (a pdfTeX file name, an editor's private
// copy of an annotation's text) is a channel nobody listed.
constexpr std::array<std::string_view, 21> form_keys{
    "Type",   "Subtype",     "FormType",  "BBox",         "Matrix",       "Resources",
    "Group",  "Metadata",    "PieceInfo", "LastModified", "StructParent", "StructParents",
    "OC",     "Name",        "AF",        "Measure",      "PtData",       "Length",
    "Filter", "DecodeParms", "DL"};
// ISO 32000-1 tables 5, 89 and 146 (read): the keys of an image XObject kept,
// /Matte of a soft mask among them; /ID (a web capture identifier) and /OPI go.
constexpr std::array<std::string_view, 24> image_keys{
    "Type",   "Subtype",     "Width", "Height",       "ColorSpace",  "BitsPerComponent",
    "Intent", "ImageMask",   "Mask",  "Decode",       "Interpolate", "Alternates",
    "SMask",  "SMaskInData", "Name",  "StructParent", "Metadata",    "OC",
    "AF",     "Measure",     "Matte", "Length",       "Filter",      "DecodeParms"};

template <std::size_t N>
bool listed(const std::array<std::string_view, N>& list, std::string_view key) {
  return std::ranges::find(list, key) != list.end();
}

ObjectGraph& graph_of(Scan& scan) {
  return *scan.graph;
}

std::size_t count_key(const Scan& scan, std::string_view key) {
  return static_cast<std::size_t>(std::ranges::count_if(
      scan.dictionaries, [&](const Dictionary* dictionary) { return dictionary->contains(key); }));
}

std::size_t count_page_key(const Scan& scan, std::string_view key) {
  return static_cast<std::size_t>(std::ranges::count_if(
      scan.pages, [&](const Dictionary& page) { return page.contains(key); }));
}

Object catalog_entry(Scan& scan, std::string_view key) {
  return graph_of(scan).resolve_entry(scan.catalog, key);
}

std::size_t one_if(bool present) {
  return present ? 1U : 0U;
}

std::size_t at_least_one(bool present, std::size_t count) {
  return present ? std::max<std::size_t>(count, 1) : count;
}

// The leaf pairs of a name tree (/Names) or a number tree (/Nums).
std::size_t tree_leaves(ObjectGraph& graph, const Object& root, std::string_view leaves) {
  std::size_t count = 0;
  std::vector<Object> pending{root};
  std::set<std::uint32_t> seen;
  std::size_t visits = 0;
  while (!pending.empty() && ++visits < walk_bound) {
    const Object current = pending.back();
    pending.pop_back();
    if (const auto reference = current.reference();
        reference && !seen.insert(reference->number).second) {
      continue;
    }
    const Object node = graph.resolve(current);
    const Dictionary* dictionary = node.dictionary();
    if (dictionary == nullptr) {
      continue;
    }
    if (const Object::Array* pairs = graph.resolve_entry(*dictionary, leaves).array()) {
      count += pairs->size() / 2;
    }
    if (const Object::Array* kids = graph.resolve_entry(*dictionary, "Kids").array()) {
      pending.insert(pending.end(), kids->begin(), kids->end());
    }
  }
  return count;
}

// The nodes reached from root through the keys named, each counted when
// counted() says so.
template <typename Counted>
std::size_t linked_nodes(ObjectGraph& graph, const Object& root,
                         std::initializer_list<std::string_view> links, Counted counted) {
  std::size_t count = 0;
  std::vector<Object> pending{root};
  std::set<std::uint32_t> seen;
  std::size_t visits = 0;
  while (!pending.empty() && ++visits < walk_bound) {
    const Object current = pending.back();
    pending.pop_back();
    if (const auto reference = current.reference();
        reference && !seen.insert(reference->number).second) {
      continue;
    }
    const Object node = graph.resolve(current);
    if (const Object::Array* items = node.array()) {
      pending.insert(pending.end(), items->begin(), items->end());
      continue;
    }
    const Dictionary* dictionary = node.dictionary();
    if (dictionary == nullptr) {
      continue;
    }
    if (counted(*dictionary)) {
      ++count;
    }
    for (const std::string_view link : links) {
      if (dictionary->contains(link)) {
        pending.push_back(dictionary->get(link));
      }
    }
  }
  return count;
}

// The catalog's XMP packet decoded, or no value.
std::optional<std::string> catalog_packet(Scan& scan) {
  const Object value = catalog_entry(scan, "Metadata");
  const syntax::Stream* stream = value.stream();
  if (stream == nullptr) {
    return std::nullopt;
  }
  const syntax::Filters::Result decoded = graph_of(scan).document().decode(*stream);
  return std::string(reinterpret_cast<const char*>(decoded.bytes.data()), decoded.bytes.size());
}

// ---- detection, one function per row ----------------------------------------

std::size_t detect_revisions(Scan& scan) {
  const platform::ByteSpan bytes = graph_of(scan).document().bytes();
  std::size_t sections = 0;
  std::size_t from = 0;
  while (const auto found = syntax::Parser::find_keyword(bytes, from, "startxref")) {
    ++sections;
    from = *found + 9;
  }
  const std::string_view head(reinterpret_cast<const char*>(bytes.data()),
                              std::min<std::size_t>(bytes.size(), 1024));
  // A linearized file carries a first-page section of its own (ISO 32000-1
  // Annex F, recalled), which is not an earlier revision.
  const std::size_t linearized = head.find("/Linearized") != std::string_view::npos ? 1 : 0;
  return sections > 1 + linearized ? sections - 1 - linearized : 0;
}

std::size_t detect_unreferenced(Scan& scan) {
  syntax::Document& document = graph_of(scan).document();
  std::size_t count = 0;
  for (const auto& [number, entry] : document.entries()) {
    if (entry.type == syntax::CrossReference::Entry::Type::free || scan.reached.contains(number)) {
      continue;
    }
    const Object value = document.object({number, 0});
    if (value.is_null()) {
      continue;
    }
    const Dictionary* dictionary = value.dictionary_or_stream_dictionary();
    if (dictionary != nullptr &&
        (dictionary->get("Type").is_name("XRef") || dictionary->get("Type").is_name("ObjStm") ||
         dictionary->contains("Linearized"))) {
      continue;
    }
    ++count;
  }
  return count;
}

std::size_t detect_thumbnails(Scan& scan) {
  return count_page_key(scan, "Thumb");
}

std::size_t detect_piece_info(Scan& scan) {
  return count_key(scan, "PieceInfo");
}

std::size_t detect_last_modified(Scan& scan) {
  return count_key(scan, "LastModified");
}

std::size_t detect_object_metadata(Scan& scan) {
  const std::size_t all = count_key(scan, "Metadata");
  const std::size_t catalog = scan.catalog.contains("Metadata") ? 1 : 0;
  return all > catalog ? all - catalog : 0;
}

std::size_t detect_javascript(Scan& scan) {
  return static_cast<std::size_t>(
      std::ranges::count_if(scan.dictionaries, [](const Dictionary* dictionary) {
        return dictionary->contains("JS") || dictionary->get("S").is_name("JavaScript");
      }));
}

std::size_t detect_embedded_files(Scan& scan) {
  return count_key(scan, "EF") + count_key(scan, "AF");
}

std::size_t detect_annotations(Scan& scan) {
  std::size_t count = 0;
  for (const Dictionary& page : scan.pages) {
    if (const Object::Array* annotations = graph_of(scan).resolve_entry(page, "Annots").array()) {
      count += annotations->size();
    }
  }
  return count;
}

std::size_t detect_forms(Scan& scan) {
  const Object form = catalog_entry(scan, "AcroForm");
  const Dictionary* dictionary = form.dictionary();
  if (dictionary == nullptr) {
    return 0;
  }
  std::size_t count = dictionary->contains("XFA") ? 1 : 0;
  if (const Object::Array* fields = graph_of(scan).resolve_entry(*dictionary, "Fields").array()) {
    count += fields->size();
  }
  return std::max<std::size_t>(count, 1);
}

std::size_t detect_outlines(Scan& scan) {
  const Object outlines = scan.catalog.get("Outlines");
  if (outlines.is_null()) {
    return 0;
  }
  const Object first = graph_of(scan).resolve(outlines);
  const Dictionary* root = first.dictionary();
  const std::size_t items =
      root == nullptr ? 0
                      : linked_nodes(graph_of(scan), root->get("First"), {"First", "Next"},
                                     [](const Dictionary&) { return true; });
  return std::max<std::size_t>(items, 1);
}

std::size_t detect_named_destinations(Scan& scan) {
  std::size_t count = 0;
  bool present = false;
  if (const Dictionary* dests = catalog_entry(scan, "Dests").dictionary()) {
    present = true;
    count += dests->size();
  }
  if (const Dictionary* names = catalog_entry(scan, "Names").dictionary()) {
    present = true;
    for (const auto& entry : names->entries()) {
      count += tree_leaves(graph_of(scan), entry.second, "Names");
    }
  }
  return at_least_one(present, count);
}

std::size_t detect_structure(Scan& scan) {
  const Object root = scan.catalog.get("StructTreeRoot");
  if (root.is_null()) {
    return 0;
  }
  const Object tree = graph_of(scan).resolve(root);
  const Dictionary* dictionary = tree.dictionary();
  const std::size_t elements =
      dictionary == nullptr
          ? 0
          : linked_nodes(graph_of(scan), dictionary->get("K"), {"K"},
                         [](const Dictionary& node) { return node.contains("S"); });
  return std::max<std::size_t>(elements, 1);
}

std::size_t detect_optional_content(Scan& scan) {
  const Object properties = catalog_entry(scan, "OCProperties");
  const Dictionary* dictionary = properties.dictionary();
  if (dictionary == nullptr) {
    return 0;
  }
  const Object::Array* groups = graph_of(scan).resolve_entry(*dictionary, "OCGs").array();
  return std::max<std::size_t>(groups != nullptr ? groups->size() : 0, 1);
}

// The groups of /OCGs the default configuration turns off: with /BaseState
// /OFF those its /ON list does not name, else those its /OFF list names. A
// group written as a direct object, which no list can name, is on, as
// text::OptionalContent reads it.
std::size_t off_groups(Scan& scan) {
  const Object properties = catalog_entry(scan, "OCProperties");
  const Dictionary* dictionary = properties.dictionary();
  if (dictionary == nullptr) {
    return 0;
  }
  const Object groups = graph_of(scan).resolve_entry(*dictionary, "OCGs");
  const Object defaults = graph_of(scan).resolve_entry(*dictionary, "D");
  const Dictionary* configuration = defaults.dictionary();
  if (groups.array() == nullptr || configuration == nullptr) {
    return 0;
  }
  const auto numbers = [&](std::string_view key) {
    std::set<std::uint32_t> out;
    const Object list = graph_of(scan).resolve_entry(*configuration, key);
    if (const Object::Array* items = list.array()) {
      for (const Object& item : *items) {
        if (const auto reference = item.reference()) {
          out.insert(reference->number);
        }
      }
    }
    return out;
  };
  const bool base_off = graph_of(scan).resolve_entry(*configuration, "BaseState").is_name("OFF");
  const std::set<std::uint32_t> on = numbers("ON");
  const std::set<std::uint32_t> off = numbers("OFF");
  std::set<std::uint32_t> turned_off;
  for (const Object& item : *groups.array()) {
    const auto reference = item.reference();
    if (reference &&
        (off.contains(reference->number) || (base_off && !on.contains(reference->number)))) {
      turned_off.insert(reference->number);
    }
  }
  return turned_off.size();
}

std::size_t detect_page_labels(Scan& scan) {
  const Object labels = scan.catalog.get("PageLabels");
  if (labels.is_null()) {
    return 0;
  }
  return std::max<std::size_t>(tree_leaves(graph_of(scan), labels, "Nums"), 1);
}

std::size_t detect_xmp_history(Scan& scan) {
  const std::optional<std::string> packet = catalog_packet(scan);
  return packet && packet->find("xmpMM:History") != std::string::npos ? 1 : 0;
}

std::size_t detect_threads(Scan& scan) {
  const Object threads = catalog_entry(scan, "Threads");
  if (const Object::Array* list = threads.array()) {
    return std::max<std::size_t>(list->size(), 1);
  }
  return threads.is_null() ? count_page_key(scan, "B") : 1;
}

std::size_t detect_web_capture(Scan& scan) {
  return one_if(scan.catalog.contains("SpiderInfo")) + count_page_key(scan, "ID");
}

std::size_t detect_portfolio(Scan& scan) {
  return scan.catalog.contains("Collection") ? 1 : 0;
}

std::size_t detect_permissions(Scan& scan) {
  return one_if(scan.catalog.contains("Perms")) + one_if(scan.catalog.contains("Legal"));
}

std::size_t detect_encryption(Scan& scan) {
  const syntax::Document& document = graph_of(scan).document();
  return document.security() || document.trailer().contains("Encrypt") ? 1 : 0;
}

std::size_t detect_uri_base(Scan& scan) {
  return scan.catalog.contains("URI") ? 1 : 0;
}

// The kind of XObject a stream dictionary is, by its /Subtype.
enum class XObject : std::uint8_t { none, form, image };

XObject xobject_of(const Dictionary& dictionary) {
  const Object& type = dictionary.get("Type");
  if (!type.is_null() && !type.is_name("XObject")) {
    return XObject::none;
  }
  if (dictionary.get("Subtype").is_name("Form")) {
    return XObject::form;
  }
  return dictionary.get("Subtype").is_name("Image") ? XObject::image : XObject::none;
}

bool private_key(XObject kind, std::string_view key) {
  return (kind == XObject::form && !listed(form_keys, key)) ||
         (kind == XObject::image && !listed(image_keys, key));
}

std::size_t detect_xobject_private(Scan& scan) {
  std::size_t count = 0;
  for (const Object& value : scan.held) {
    if (const syntax::Stream* stream = value.stream()) {
      const XObject kind = xobject_of(stream->dictionary());
      for (const auto& entry : stream->dictionary().entries()) {
        count += one_if(private_key(kind, entry.first));
      }
    }
  }
  return count;
}

std::size_t detect_search_index(Scan& scan) {
  const Object& mode = scan.catalog.get("PageMode");
  const std::string* name = mode.name();
  const bool private_mode = name != nullptr && !listed(page_modes, *name);
  return one_if(scan.catalog.contains("SearchIndex")) + one_if(private_mode);
}

// ---- stripping, one declaration per row -------------------------------------

void strip_nothing(Edits& /*edits*/) {}

void strip_thumbnails(Edits& edits) {
  edits.page.insert("Thumb");
}

void strip_piece_info(Edits& edits) {
  edits.everywhere.insert("PieceInfo");
}

void strip_last_modified(Edits& edits) {
  edits.everywhere.insert("LastModified");
}

void strip_object_metadata(Edits& edits) {
  edits.object_metadata = true;
}

void strip_javascript(Edits& edits) {
  edits.catalog.insert({"OpenAction", "AA", "Names"});
  edits.page.insert("AA");
  edits.everywhere.insert({"AA", "JS"});
}

void strip_embedded_files(Edits& edits) {
  edits.catalog.insert("Names");
  edits.page.insert("Annots");
  edits.everywhere.insert({"AF", "EF"});
}

void strip_annotations(Edits& edits) {
  edits.page.insert("Annots");
}

void strip_forms(Edits& edits) {
  edits.catalog.insert("AcroForm");
  edits.page.insert("Annots");
}

void strip_outlines(Edits& edits) {
  edits.catalog.insert("Outlines");
}

void strip_named_destinations(Edits& edits) {
  edits.catalog.insert({"Dests", "Names"});
}

void strip_structure(Edits& edits) {
  edits.catalog.insert({"StructTreeRoot", "MarkInfo"});
  edits.page.insert({"StructParents", "Tabs"});
  edits.everywhere.insert({"StructParent", "StructParents"});
}

void strip_optional_content(Edits& edits) {
  edits.catalog.insert("OCProperties");
  edits.everywhere.insert("OC");
}

void strip_page_labels(Edits& edits) {
  edits.catalog.insert("PageLabels");
}

void strip_xmp_history(Edits& edits) {
  edits.xmp_history = true;
}

void strip_threads(Edits& edits) {
  edits.catalog.insert("Threads");
  edits.page.insert("B");
}

void strip_web_capture(Edits& edits) {
  edits.catalog.insert("SpiderInfo");
  edits.page.insert("ID");
}

void strip_portfolio(Edits& edits) {
  edits.catalog.insert("Collection");
}

void strip_permissions(Edits& edits) {
  edits.catalog.insert({"Perms", "Legal"});
}

void strip_uri_base(Edits& edits) {
  edits.catalog.insert("URI");
}

void strip_search_index(Edits& edits) {
  edits.catalog.insert("SearchIndex");
}

void strip_xobject_private(Edits& edits) {
  edits.xobject_private = true;
}

// The table: docs/DESIGN.md section 15, in its order. Earlier revisions,
// unreferenced objects and the encryption are stripped by writing a fresh,
// unencrypted file of the reachable objects only, so their rows declare no
// edit.
constexpr std::array<ChannelTable::Row, 24> table{{
    {"ch_revisions", "earlier revisions (incremental updates)", &detect_revisions, &strip_nothing},
    {"ch_unreferenced", "unreferenced objects", &detect_unreferenced, &strip_nothing},
    {"ch_thumbnail", "page thumbnails /Thumb", &detect_thumbnails, &strip_thumbnails},
    {"ch_pieceinfo", "private application data /PieceInfo", &detect_piece_info, &strip_piece_info},
    {"ch_last_modified", "modification stamps /LastModified", &detect_last_modified,
     &strip_last_modified},
    {"ch_object_metadata", "object-level /Metadata", &detect_object_metadata,
     &strip_object_metadata},
    {"ch_javascript", "JavaScript and automatic actions", &detect_javascript, &strip_javascript},
    {"ch_embedded_file", "embedded and associated files", &detect_embedded_files,
     &strip_embedded_files},
    {"ch_annotation", "annotations and their appearances", &detect_annotations, &strip_annotations},
    {"ch_acroform", "interactive forms /AcroForm and /XFA", &detect_forms, &strip_forms},
    {"ch_outline", "outlines /Outlines", &detect_outlines, &strip_outlines},
    {"ch_named_dest", "named destinations and name trees", &detect_named_destinations,
     &strip_named_destinations},
    {"ch_struct_tree", "the structure tree", &detect_structure, &strip_structure},
    {"ch_optional_content", "optional content: groups, and content in groups turned off",
     &detect_optional_content,
     &strip_optional_content},
    {"ch_page_labels", "page labels /PageLabels", &detect_page_labels, &strip_page_labels},
    {"ch_xmp_history", "XMP history and identifiers", &detect_xmp_history, &strip_xmp_history},
    {"ch_threads", "article threads /Threads and beads /B", &detect_threads, &strip_threads},
    {"ch_web_capture", "web capture /SpiderInfo and page /ID", &detect_web_capture,
     &strip_web_capture},
    {"ch_portfolio", "portfolios /Collection", &detect_portfolio, &strip_portfolio},
    {"ch_perms_legal", "permissions /Perms and attestations /Legal", &detect_permissions,
     &strip_permissions},
    {"ch_encrypt", "encryption /Encrypt", &detect_encryption, &strip_nothing},
    {"ch_uri_base", "the base URI /URI", &detect_uri_base, &strip_uri_base},
    {"ch_search_index", "search indexes", &detect_search_index, &strip_search_index},
    {"ch_xobject_private", "private keys of form and image XObjects", &detect_xobject_private,
     &strip_xobject_private},
}};

// ---- the rewrite pass ---------------------------------------------------------

enum class Role : std::uint8_t { other, catalog, page, form, image };

class Rewriter {
 public:
  Rewriter(ObjectGraph& graph, const Edits& edits, std::set<std::uint32_t> pages)
      : graph_(graph), edits_(edits), pages_(std::move(pages)), root_(graph.root()) {}

  // Rewrites every object reachable from the trailer; an object is rewritten
  // before its references are followed, so what an edit drops is never
  // visited.
  void run() {
    std::vector<std::uint32_t> stack;
    for (const std::string_view key : {"Info", "Root"}) {
      if (const auto reference = graph_.trailer().get(key).reference()) {
        stack.push_back(reference->number);
      }
    }
    std::set<std::uint32_t> seen;
    std::vector<Object::Reference> found;
    while (!stack.empty()) {
      const std::uint32_t number = stack.back();
      stack.pop_back();
      if (!seen.insert(number).second) {
        continue;
      }
      Object value = graph_.get(number);
      if (value.is_null()) {
        continue;
      }
      const Role role = number == root_           ? Role::catalog
                        : pages_.contains(number) ? Role::page
                                                  : Role::other;
      bool changed = false;
      Object rewritten = rewrite_top(value, role, changed);
      if (changed) {
        graph_.set(number, rewritten);
        value = std::move(rewritten);
      }
      found.clear();
      Reachability::references(value, found);
      for (const Object::Reference reference : std::views::reverse(found)) {
        if (!seen.contains(reference.number)) {
          stack.push_back(reference.number);
        }
      }
    }
  }

 private:
  Object rewrite_top(const Object& value, Role role, bool& changed) {
    if (const syntax::Stream* stream = value.stream()) {
      const XObject kind = xobject_of(stream->dictionary());
      const Role own = kind == XObject::form    ? Role::form
                       : kind == XObject::image ? Role::image
                                                : Role::other;
      Dictionary dictionary = rewrite_dictionary(stream->dictionary(), own, changed);
      return changed ? Object::make_stream(stream->with_dictionary(std::move(dictionary))) : value;
    }
    if (const Dictionary* dictionary = value.dictionary()) {
      Dictionary rewritten = rewrite_dictionary(*dictionary, role, changed);
      return changed ? Object::make_dictionary(std::move(rewritten)) : value;
    }
    return rewrite_value(value, changed);
  }

  Object rewrite_value(const Object& value, bool& changed) {
    if (const Object::Array* items = value.array()) {
      Object::Array out;
      out.reserve(items->size());
      bool any = false;
      for (const Object& item : *items) {
        out.push_back(rewrite_value(item, any));
      }
      changed = changed || any;
      return any ? Object::make_array(std::move(out)) : value;
    }
    if (const Dictionary* dictionary = value.dictionary()) {
      bool any = false;
      Dictionary out = rewrite_dictionary(*dictionary, Role::other, any);
      changed = changed || any;
      return any ? Object::make_dictionary(std::move(out)) : value;
    }
    return value;
  }

  bool dropped(std::string_view key, Role role) const {
    if (edits_.everywhere.contains(key)) {
      return true;
    }
    if (edits_.object_metadata && key == "Metadata" && role != Role::catalog) {
      return true;
    }
    if (role == Role::catalog) {
      return !listed(catalog_keys, key) || edits_.catalog.contains(key);
    }
    if (role == Role::page) {
      return !listed(page_keys, key) || edits_.page.contains(key);
    }
    if (edits_.xobject_private && role == Role::form) {
      return private_key(XObject::form, key);
    }
    if (edits_.xobject_private && role == Role::image) {
      return private_key(XObject::image, key);
    }
    return false;
  }

  Dictionary rewrite_dictionary(const Dictionary& dictionary, Role role, bool& changed) {
    Dictionary out;
    for (const auto& [key, value] : dictionary.entries()) {
      if (dropped(key, role)) {
        changed = true;
        continue;
      }
      if (role == Role::catalog && key == "ViewerPreferences") {
        out.set(key, viewer_preferences(value));
        changed = true;
        continue;
      }
      if (role == Role::catalog && key == "PageMode") {
        out.set(key, Object::make_name(kept_mode(value)));
        changed = changed || !(out.get(key) == value);
        continue;
      }
      out.set(key, rewrite_value(value, changed));
    }
    return out;
  }

  // A page mode whose panel the output still has, else /UseNone; the layers
  // panel of /UseOC goes with the optional content.
  static std::string kept_mode(const Object& value) {
    const std::string* name = value.name();
    if (name != nullptr && (*name == "UseNone" || *name == "FullScreen")) {
      return *name;
    }
    return "UseNone";
  }

  Object viewer_preferences(const Object& value) {
    const Object resolved = graph_.resolve(value);
    Dictionary out;
    if (const Dictionary* preferences = resolved.dictionary()) {
      for (const auto& [key, entry] : preferences->entries()) {
        if (!listed(viewer_keys, key)) {
          continue;
        }
        if (key == "NonFullScreenPageMode") {
          out.set(key, Object::make_name(kept_mode(graph_.resolve(entry))));
        } else {
          out.set(key, graph_.resolve(entry));
        }
      }
    }
    return Object::make_dictionary(std::move(out));
  }

  ObjectGraph& graph_;
  const Edits& edits_;
  std::set<std::uint32_t> pages_;
  std::uint32_t root_;
};

// ---- the XMP packet -------------------------------------------------------------

constexpr std::string_view media_prefix = "xmpMM:";

bool is_name_byte(char c) {
  return c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '>' && c != '/' && c != '=';
}

// Whether an xmpMM property named name holding value is an identifier the
// metadata writer left for the file writer to patch.
bool kept_identifier(std::string_view name, std::string_view value) {
  return (name == "xmpMM:DocumentID" && value == DocumentId::xmp_document_placeholder) ||
         (name == "xmpMM:InstanceID" && value == DocumentId::xmp_instance_placeholder);
}

// The packet without its xmpMM elements and attributes; no value when an
// element does not close, which is a packet that does not scan.
std::optional<std::string> without_media_management(const std::string& packet) {
  std::string out;
  out.reserve(packet.size());
  bool in_tag = false;
  char quote = 0;
  std::size_t i = 0;
  while (i < packet.size()) {
    const char c = packet[i];
    if (in_tag && quote != 0) {
      quote = c == quote ? 0 : quote;
      out += c;
      ++i;
      continue;
    }
    if (!in_tag && c == '<' && packet.compare(i + 1, media_prefix.size(), media_prefix) == 0) {
      std::size_t end = i + 1 + media_prefix.size();
      while (end < packet.size() && is_name_byte(packet[end])) {
        ++end;
      }
      const std::string name = packet.substr(i + 1, end - i - 1);
      // The start tag's end, quotes respected.
      char inner = 0;
      std::size_t close = end;
      while (close < packet.size() && (inner != 0 || packet[close] != '>')) {
        if (inner != 0) {
          inner = packet[close] == inner ? 0 : inner;
        } else if (packet[close] == '"' || packet[close] == '\'') {
          inner = packet[close];
        }
        ++close;
      }
      if (close >= packet.size()) {
        return std::nullopt;
      }
      if (packet[close - 1] == '/') {
        i = close + 1;
        continue;
      }
      // The matching end tag, nested elements of the same name counted.
      int depth = 1;
      std::size_t at = close + 1;
      const std::string open = "<" + name;
      const std::string shut = "</" + name + ">";
      if (const std::size_t first_shut = packet.find(shut, at);
          first_shut != std::string::npos &&
          kept_identifier(name, std::string_view(packet).substr(at, first_shut - at))) {
        out.append(packet, i, first_shut + shut.size() - i);
        i = first_shut + shut.size();
        continue;
      }
      while (depth > 0) {
        const std::size_t next_open = packet.find(open, at);
        const std::size_t next_shut = packet.find(shut, at);
        if (next_shut == std::string::npos) {
          return std::nullopt;
        }
        if (next_open != std::string::npos && next_open < next_shut &&
            next_open + open.size() < packet.size() &&
            !is_name_byte(packet[next_open + open.size()])) {
          ++depth;
          at = next_open + open.size();
        } else {
          --depth;
          at = next_shut + shut.size();
        }
      }
      i = at;
      continue;
    }
    if (in_tag && (c == ' ' || c == '\t' || c == '\r' || c == '\n') &&
        packet.compare(i + 1, media_prefix.size(), media_prefix) == 0) {
      // An attribute: its name, "=", and its quoted value.
      std::size_t at = i + 1;
      while (at < packet.size() && is_name_byte(packet[at])) {
        ++at;
      }
      const std::string_view attribute = std::string_view(packet).substr(i + 1, at - i - 1);
      while (at < packet.size() && (packet[at] == ' ' || packet[at] == '=')) {
        ++at;
      }
      if (at < packet.size() && (packet[at] == '"' || packet[at] == '\'')) {
        const std::size_t end = packet.find(packet[at], at + 1);
        if (end == std::string::npos) {
          return std::nullopt;
        }
        if (!kept_identifier(attribute, std::string_view(packet).substr(at + 1, end - at - 1))) {
          i = end + 1;
          continue;
        }
      }
    }
    if (!in_tag && c == '<' && i + 1 < packet.size() && packet[i + 1] != '!' &&
        packet[i + 1] != '?') {
      in_tag = true;
    } else if (in_tag && c == '>') {
      in_tag = false;
    } else if (in_tag && (c == '"' || c == '\'')) {
      quote = c;
    }
    out += c;
    ++i;
  }
  return out;
}

void strip_xmp(ObjectGraph& graph, std::vector<ChannelTable::Warning>& warnings) {
  const std::uint32_t root = graph.root();
  const Object catalog_value = graph.get(root);
  const Dictionary* catalog = catalog_value.dictionary();
  if (catalog == nullptr || !catalog->contains("Metadata")) {
    return;
  }
  const Object& entry = catalog->get("Metadata");
  const Object value = graph.resolve(entry);
  const syntax::Stream* stream = value.stream();
  if (stream == nullptr) {
    return;
  }
  const syntax::Filters::Result decoded = graph.document().decode(*stream);
  std::optional<std::string> edited;
  if (!decoded.damaged && decoded.remaining.empty()) {
    edited = without_media_management(
        std::string(reinterpret_cast<const char*>(decoded.bytes.data()), decoded.bytes.size()));
  }
  if (!edited) {
    Dictionary updated = *catalog;
    updated.erase("Metadata");
    graph.set(root, Object::make_dictionary(std::move(updated)));
    warnings.push_back({"xmp_unreadable",
                        "the catalog's XMP packet does not scan, so it is dropped from the output",
                        std::nullopt});
    return;
  }
  if (edited->size() == decoded.bytes.size()) {
    return;
  }
  const syntax::Stream rewritten =
      StreamCopier::plain(stream->dictionary(), platform::as_bytes(*edited));
  if (const auto reference = entry.reference()) {
    graph.set(reference->number, Object::make_stream(rewritten));
  } else {
    Dictionary updated = *catalog;
    updated.set("Metadata", Object::make_reference(graph.add(Object::make_stream(rewritten))));
    graph.set(root, Object::make_dictionary(std::move(updated)));
  }
}

// /Redact annotations per page, as warnings.
std::size_t pending_redactions(Scan& scan, const std::vector<std::uint32_t>& page_labels,
                               std::vector<ChannelTable::Warning>& warnings) {
  std::size_t total = 0;
  for (std::size_t i = 0; i < scan.pages.size(); ++i) {
    const Object annotations = graph_of(scan).resolve_entry(scan.pages[i], "Annots");
    const Object::Array* list = annotations.array();
    if (list == nullptr) {
      continue;
    }
    std::size_t count = 0;
    for (const Object& item : *list) {
      const Object annotation = graph_of(scan).resolve(item);
      if (const Dictionary* dictionary = annotation.dictionary();
          dictionary != nullptr && dictionary->get("Subtype").is_name("Redact")) {
        ++count;
      }
    }
    if (count == 0) {
      continue;
    }
    total += count;
    const std::uint32_t page =
        i < page_labels.size() ? page_labels[i] : static_cast<std::uint32_t>(i + 1);
    warnings.push_back({"pending_redactions",
                        "page " + std::to_string(page) + " holds " + std::to_string(count) +
                            " /Redact annotation(s) never applied: the text they mark is still "
                            "in the input; apply them or redact the text with pdfkit_redact_text",
                        page});
  }
  return total;
}

}  // namespace

std::span<const ChannelTable::Row> ChannelTable::rows() noexcept {
  return table;
}

ChannelTable::Scan ChannelTable::scan(ObjectGraph& graph) {
  Scan scan;
  scan.graph = &graph;
  scan.root = graph.root();
  const Object catalog = graph.get(scan.root);
  if (const Dictionary* dictionary = catalog.dictionary()) {
    scan.catalog = *dictionary;
  }
  // The pages, depth first through /Kids.
  std::vector<Object> pending{scan.catalog.get("Pages")};
  std::set<std::uint32_t> nodes;
  std::size_t visits = 0;
  while (!pending.empty() && ++visits < walk_bound) {
    const Object current = pending.back();
    pending.pop_back();
    const auto reference = current.reference();
    if (reference && !nodes.insert(reference->number).second) {
      continue;
    }
    const Object node = graph.resolve(current);
    const Dictionary* dictionary = node.dictionary();
    if (dictionary == nullptr) {
      continue;
    }
    const Object kids = graph.resolve_entry(*dictionary, "Kids");
    if (!dictionary->get("Type").is_name("Page") && kids.array() != nullptr) {
      for (const Object& kid : std::views::reverse(*kids.array())) {
        pending.push_back(kid);
      }
      continue;
    }
    scan.page_numbers.push_back(reference ? reference->number : 0);
    scan.pages.push_back(*dictionary);
  }
  // Every reachable object, its dictionaries collected.
  std::vector<std::uint32_t> stack;
  for (const std::string_view key : {"Info", "Root"}) {
    if (const auto reference = graph.trailer().get(key).reference()) {
      stack.push_back(reference->number);
    }
  }
  if (const auto encrypt = graph.document().trailer().get("Encrypt").reference()) {
    scan.reached.insert(encrypt->number);
  }
  std::vector<const Object*> inner;
  while (!stack.empty()) {
    const std::uint32_t number = stack.back();
    stack.pop_back();
    if (scan.reached.contains(number)) {
      continue;
    }
    const Object value = graph.get(number);
    if (value.is_null()) {
      continue;
    }
    scan.reached.insert(number);
    scan.held.push_back(value);
    inner.assign(1, &scan.held.back());
    while (!inner.empty()) {
      const Object* current = inner.back();
      inner.pop_back();
      if (const auto reference = current->reference()) {
        if (!scan.reached.contains(reference->number)) {
          stack.push_back(reference->number);
        }
      } else if (const Object::Array* items = current->array()) {
        for (const Object& item : *items) {
          inner.push_back(&item);
        }
      } else if (const Dictionary* dictionary = current->dictionary_or_stream_dictionary()) {
        scan.dictionaries.push_back(dictionary);
        for (const auto& entry : dictionary->entries()) {
          inner.push_back(&entry.second);
        }
      }
    }
  }
  return scan;
}

ChannelTable::Report ChannelTable::detect(ObjectGraph& graph,
                                          const std::vector<std::uint32_t>& page_labels) {
  Scan found = scan(graph);
  Report report;
  for (const Row& row : table) {
    report.counts.push_back({row.id, row.detect(found)});
  }
  report.off_groups = off_groups(found);
  report.pending_redactions = pending_redactions(found, page_labels, report.warnings);
  return report;
}

ChannelTable::Report ChannelTable::strip(ObjectGraph& graph,
                                         const std::vector<std::uint32_t>& page_labels) {
  Scan found = scan(graph);
  Report report;
  Edits edits;
  for (const Row& row : table) {
    report.counts.push_back({row.id, row.detect(found)});
    row.strip(edits);
  }
  report.off_groups = off_groups(found);
  report.pending_redactions = pending_redactions(found, page_labels, report.warnings);
  const std::set<std::uint32_t> pages(found.page_numbers.begin(), found.page_numbers.end());
  Rewriter(graph, edits, pages).run();
  if (edits.xmp_history) {
    strip_xmp(graph, report.warnings);
  }
  return report;
}

}  // namespace pdfkit::writer
