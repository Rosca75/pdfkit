// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// JsonText is the CLI's own small JSON reader. The CLI may include nothing of
// the library but its C header, so it reads the JSON the library returns (the
// catalogue of pdfkit_version) and the JSON its user writes (a redact job)
// with this. Every node keeps its raw text, so the CLI hands a section of a
// job to the library, or prints an error object, byte for byte as written;
// the library validates every byte it receives with its own strict reader.

#ifndef PDFKIT_CLI_JSON_TEXT_HPP
#define PDFKIT_CLI_JSON_TEXT_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::cli {

class JsonText {
 public:
  enum class Kind : std::uint8_t { null, boolean, number, string, array, object };

  struct Node {
    Kind kind = Kind::null;
    std::string_view raw;
    // The decoded value of a string; "true" or "false" for a boolean.
    std::string text;
    std::vector<std::pair<std::string, std::size_t>> members;
    std::vector<std::size_t> items;
  };

  // The parsed document, or no value with the byte offset of the first fault
  // in error_offset. text must outlive the result.
  [[nodiscard]] static std::optional<JsonText> parse(std::string_view text,
                                                     std::size_t& error_offset);

  [[nodiscard]] const Node& root() const { return nodes_.front(); }
  [[nodiscard]] const Node& node(std::size_t index) const { return nodes_.at(index); }
  // The member named key of an object node, or nullptr.
  [[nodiscard]] const Node* member(const Node& object, std::string_view key) const;

 private:
  std::vector<Node> nodes_;
};

}  // namespace pdfkit::cli

#endif  // PDFKIT_CLI_JSON_TEXT_HPP
