// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// CrossReference reads the cross-reference sections of a file the way its
// trailer chain names them, docs/DESIGN.md section 1.4 over ISO 32000-1
// sections 7.5.4, 7.5.5 and 7.5.8 (read): startxref searched in the last
// 2,048 bytes and required to land on "xref" or on the "n g obj" of a
// cross-reference stream; a classic table read entry by entry as three
// white-space-separated fields, so 19- and 21-byte entries read like 20-byte
// ones; a first subsection labelled 1 whose first entry is the free head of
// the list read as starting at 0 (pdf.js's correction, recalled); the stream
// form with /W, /Index and /Size; a hybrid file's /XRefStm filling only the
// numbers its table does not name; the /Prev chain followed newest first with
// a visited set, the first entry read for a number winning. When the path
// disappoints, read answers the reason and the document reconstructs
// (section 1.6).

#ifndef PDFKIT_SYNTAX_CROSS_REFERENCE_HPP
#define PDFKIT_SYNTAX_CROSS_REFERENCE_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::syntax {

class CrossReference {
 public:
  struct Entry {
    enum class Type : std::uint8_t { free, offset, compressed };
    Type type = Type::free;
    // The byte offset of an object (offset), or the number of the object
    // stream that holds it (compressed).
    std::uint64_t offset = 0;
    // The generation (offset), or the index inside the object stream
    // (compressed).
    std::uint32_t generation = 0;

    friend bool operator==(const Entry&, const Entry&) = default;
  };

  // Reads the chain starting at startxref; no value, with the reason the
  // reader must reconstruct in failure, when the path disappoints. Decoding
  // a cross-reference stream is charged to budget.
  [[nodiscard]] static std::optional<CrossReference> read(platform::ByteSpan file,
                                                          platform::Budget& budget,
                                                          std::string& failure);

  [[nodiscard]] const std::map<std::uint32_t, Entry>& entries() const noexcept { return entries_; }
  [[nodiscard]] const Entry* find(std::uint32_t number) const noexcept;
  // The newest trailer, with the keys it lacks filled from older ones.
  [[nodiscard]] const Dictionary& trailer() const noexcept { return trailer_; }
  // The byte offsets of the sections read, newest first.
  [[nodiscard]] const std::vector<std::uint64_t>& sections() const noexcept { return sections_; }
  // Leniencies applied while reading that need no reconstruction.
  [[nodiscard]] const std::vector<std::string>& notes() const noexcept { return notes_; }

  // Builds a table from entries and a trailer, as reconstruction does.
  CrossReference(std::map<std::uint32_t, Entry> entries, Dictionary trailer);

 private:
  CrossReference() = default;

  // Each answers an empty string on success or the reason it failed.
  std::string read_table(platform::ByteSpan file, std::uint64_t offset, bool first_section,
                         Dictionary& trailer);
  std::string read_stream(platform::ByteSpan file, std::uint64_t offset, platform::Budget& budget,
                          Dictionary& trailer);
  void merge_trailer(const Dictionary& older);

  std::map<std::uint32_t, Entry> entries_;
  Dictionary trailer_;
  std::vector<std::uint64_t> sections_;
  std::vector<std::string> notes_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_CROSS_REFERENCE_HPP
