// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PropertyStrings names the strings of a page's marked-content property
// lists (docs/ABI.md, the pdfkit_find section): every list of a BDC or DP
// operator in the page's walks (the page's content, then each form where it
// is drawn), written inline or named through the /Properties of the
// resources the walk reads, each numbered page-wide in walk order then
// operator order; an /OC list is left out, because every write removes it.
// Every string of a list, at any depth of its dictionaries and arrays, is
// read as a text string into UTF-8 with its path (the keys and array
// indices from the list down to it) and its surface, named by the list's key
// it lies under: /Alt "alt", /E "expansion", /ActualText "actual_text" and
// any other key "property". An /ActualText that a glyph reads as its text is
// left out, because the glyph's occurrence reports it. These are the strings
// a search rewrites and the write keeps (MarkedContentRewriter), so
// pdfkit_find reports a value there, pdfkit_redact_text takes one place back
// as a string match, and the self-check counts a removed text there. It also
// counts how many operators of a set of pages name each list (uses), so the
// write decides whether an edit of a named list can stay where the list
// lives, and replaces a string at a path.

#ifndef PDFKIT_REDACT_PROPERTY_STRINGS_HPP
#define PDFKIT_REDACT_PROPERTY_STRINGS_HPP

#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/object.hpp"
#include "text/page_content.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace pdfkit::redact {

class PropertyStrings {
 public:
  // One step from a container to an item: a dictionary key or an array
  // index.
  using Step = std::variant<std::string, std::size_t>;
  using Path = std::vector<Step>;

  enum class Surface : std::uint8_t { alt, expansion, actual_text, property };

  // Which list an operator names: an indirect list by its object number; a
  // direct entry of /Properties by the resources that hold it (a form with
  // resources of its own, or the page) and its name.
  struct Identity {
    std::uint32_t object = 0;
    std::uint32_t page = 0;
    std::uint32_t holder = 0;
    std::string name;
    auto operator<=>(const Identity&) const = default;
  };

  struct List {
    // The page-wide index.
    std::size_t index = 0;
    // The walk (an index into the page's streams) and its stream: 0 for the
    // page's content, else the form's object number.
    std::size_t walk = 0;
    std::uint32_t stream = 0;
    std::size_t operation = 0;
    // BDC or DP, and the tag operand.
    std::string op;
    syntax::Object tag;
    // The /Properties name, when the list is named.
    std::optional<std::string> name;
    // A named list's identity; the object is also set for an indirect list.
    Identity identity;
    // The list as written: the operand, or the entry the name resolves to.
    syntax::Dictionary dictionary;
    // Whether a glyph reads the list's /ActualText as its text.
    bool actual_text_read = false;
  };

  struct String {
    // The list's page-wide index.
    std::size_t list = 0;
    Surface surface = Surface::property;
    Path path;
    // As UTF-8.
    std::string text;
  };

  // A located edit of a string: [first, last) code points replaced by the
  // placeholder; order is the edit's place in the plan, for the overlap
  // rule's tie.
  struct Edit {
    Path path;
    std::size_t first = 0;
    std::size_t last = 0;
    std::string placeholder;
    std::size_t order = 0;
  };

  // The page's lists in walk order, then operator order.
  [[nodiscard]] static std::vector<List> lists(syntax::Document& document,
                                               const text::PageContent& content);

  // The strings of lists, in list order, then in the order of each list's
  // keys and items.
  [[nodiscard]] static std::vector<String> strings(syntax::Document& document,
                                                   std::span<const List> lists);

  // The name pdfkit_find writes for a surface, and the surface of a path.
  [[nodiscard]] static std::string_view surface_name(Surface surface) noexcept;

  // How many BDC and DP operators of pages name each list through
  // /Properties, a form counted once per draw.
  [[nodiscard]] static std::map<Identity, std::size_t> uses(syntax::Document& document,
                                                            const std::set<std::uint32_t>& pages);

  // text with each edit's range replaced by its placeholder under the
  // overlap rule of docs/DESIGN.md section 11: an edit that starts earlier
  // keeps the overlap, of two that start together the longer, of two equal
  // ranges the one recorded first; two that overlap or touch with one
  // placeholder are one.
  [[nodiscard]] static std::u32string edited(std::u32string_view text, std::vector<Edit> edits);

  // dictionary with the edits applied to the strings their paths name (the
  // containers along a path read through references and written directly);
  // no value when no edit names a string.
  [[nodiscard]] static std::optional<syntax::Dictionary> apply(syntax::Document& document,
                                                               const syntax::Dictionary& dictionary,
                                                               std::span<const Edit> edits);

  // value with every reference replaced by what it names, at most 32 deep,
  // so it can be written inside a content stream, which holds no reference.
  [[nodiscard]] static syntax::Object direct(syntax::Document& document,
                                             const syntax::Object& value);

  // The characters of text glued to [first, last): those before first and
  // those from last on, up to white space on each side, as UTF-8.
  [[nodiscard]] static std::pair<std::string, std::string> glued(std::u32string_view text,
                                                                 std::size_t first,
                                                                 std::size_t last);

  // The deterministic key of a path, for comparing and ordering edits.
  [[nodiscard]] static std::string path_key(const Path& path);
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_PROPERTY_STRINGS_HPP
