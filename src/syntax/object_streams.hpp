// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ObjectStreams parses and caches the object streams of ISO 32000-1 section
// 7.5.7 (read), docs/DESIGN.md section 1.5: a stream's /N pairs of object
// number and offset, offsets relative to /First, each object parsed once at
// its offset and kept per stream. An object stream never holds a stream or a
// generation other than 0; an entry that would is a fault for that entry
// only. The caller decodes the stream (through the filters, the security
// handler and the budget) and hands the bytes in, so this class holds no
// reference to the document.

#ifndef PDFKIT_SYNTAX_OBJECT_STREAMS_HPP
#define PDFKIT_SYNTAX_OBJECT_STREAMS_HPP

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pdfkit::syntax {

class ObjectStreams {
 public:
  // The objects of one stream in the order of its header.
  struct Contents {
    std::vector<std::pair<std::uint32_t, Object>> objects;
    std::vector<std::string> repairs;
  };

  // Parses the decoded bytes of an object stream whose dictionary is
  // dictionary; name identifies it in the repairs ("object 12 0").
  [[nodiscard]] static Contents parse(platform::ByteSpan decoded, const Dictionary& dictionary,
                                      std::string_view name);

  // The cached contents of the stream numbered stream, or nullptr.
  [[nodiscard]] const Contents* find(std::uint32_t stream) const noexcept;
  const Contents& store(std::uint32_t stream, Contents contents);

  // The object number at index in contents, answered by index first and by
  // number when the index names another object, as pdf.js looks it up
  // (recalled); nullptr when the stream does not hold it.
  [[nodiscard]] static const Object* lookup(const Contents& contents, std::uint32_t number,
                                            std::uint32_t index) noexcept;

 private:
  std::map<std::uint32_t, Contents> cache_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_OBJECT_STREAMS_HPP
