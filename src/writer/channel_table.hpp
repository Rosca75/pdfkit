// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ChannelTable is the table of hidden channels of docs/DESIGN.md section 15:
// the places in a PDF where text or pixels survive outside the page's drawn
// content. It is the single place a channel is named. Each row has its id
// (the dataset's taxonomy tag), its name, a detect function that counts the
// channel's occurrences, and a strip function that declares the edit the
// writer makes; one rewrite pass over every object reachable from the new
// trailer applies the declared edits, then the catalog and page allow-lists
// of section 15, so a key no row names is dropped from the catalog and the
// pages rather than copied. Every row has a planted-marker unit test.
//
// What the table keeps as it is, and why, is not a row: fonts, output
// intents, colour spaces, graphics states, shadings and patterns draw the page
// and name no text (a removed glyph's width and ToUnicode entries name no text
// either). The Info dictionary is kept: it is metadata, read and rewritten by
// the metadata functions, not a hidden channel (CLAUDE.md section 2).

#ifndef PDFKIT_WRITER_CHANNEL_TABLE_HPP
#define PDFKIT_WRITER_CHANNEL_TABLE_HPP

#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"
#include "writer/object_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::writer {

class ChannelTable {
 public:
  // What the detect functions read: one walk over every object reachable
  // from the graph's trailer.
  struct Scan {
    ObjectGraph* graph = nullptr;
    std::uint32_t root = 0;
    syntax::Dictionary catalog;
    // The page objects, in tree order, and their dictionaries.
    std::vector<std::uint32_t> page_numbers;
    std::vector<syntax::Dictionary> pages;
    // Every dictionary reachable (stream dictionaries and direct dictionaries
    // included), kept alive by held.
    std::vector<const syntax::Dictionary*> dictionaries;
    std::vector<syntax::Object> held;
    // The numbers reached, /Length targets and the source's /Encrypt
    // included.
    std::set<std::uint32_t> reached;
  };

  // The edits the rows declare, applied in one pass.
  struct Edits {
    std::set<std::string, std::less<>> everywhere;
    std::set<std::string, std::less<>> catalog;
    std::set<std::string, std::less<>> page;
    bool object_metadata = false;
    bool optional_content = false;
    bool xmp_history = false;
    // The keys of form and image XObjects outside their allow-lists go.
    bool xobject_private = false;
  };

  struct Row {
    std::string_view id;
    std::string_view name;
    std::size_t (*detect)(Scan& scan);
    void (*strip)(Edits& edits);
  };

  struct Count {
    std::string_view id;
    std::size_t count = 0;
  };

  struct Warning {
    std::string code;
    std::string message;
    std::optional<std::uint32_t> page;
  };

  struct Report {
    // Per row, in table order, the occurrences found.
    std::vector<Count> counts;
    // /Redact annotations never applied, per page.
    std::size_t pending_redactions = 0;
    std::vector<Warning> warnings;
  };

  // The rows in the order of docs/DESIGN.md section 15.
  [[nodiscard]] static std::span<const Row> rows() noexcept;

  // The channels present in graph. page_labels maps a page's position in the
  // graph's tree to the page number a warning names.
  [[nodiscard]] static Report detect(ObjectGraph& graph,
                                     const std::vector<std::uint32_t>& page_labels = {});

  // Detects, then strips every channel from graph; the report counts what
  // was stripped.
  [[nodiscard]] static Report strip(ObjectGraph& graph,
                                    const std::vector<std::uint32_t>& page_labels = {});

  // The scan of graph.
  [[nodiscard]] static Scan scan(ObjectGraph& graph);
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_CHANNEL_TABLE_HPP
