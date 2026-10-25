// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// FileWriter turns an object graph into a fresh PDF file, docs/DESIGN.md
// section 17: the header (the larger of the input's version and 1.5, the
// cross-reference stream's minimum, then a comment of four bytes above 127,
// which marks the file binary), every reachable object as an indirect object
// of its own under its new number, in numbering order, each stream through
// the stream copier, then the cross-reference stream and the /ID patched from
// the file's own hash. No object stream is written (an author decision of
// section 17), and the file is never encrypted (section 2).

#ifndef PDFKIT_WRITER_FILE_WRITER_HPP
#define PDFKIT_WRITER_FILE_WRITER_HPP

#include "writer/object_graph.hpp"

#include <cstddef>
#include <string>

namespace pdfkit::writer {

class FileWriter {
 public:
  struct Result {
    std::string bytes;
    // The objects written, the cross-reference stream left out.
    std::size_t objects = 0;
    // The distinct references to objects that do not exist, written as null.
    std::size_t missing_references = 0;
    // The /ID element, in hexadecimal.
    std::string id;
  };

  [[nodiscard]] static Result write(ObjectGraph& graph);

  // The header version: the larger of version and 1.5.
  [[nodiscard]] static std::string header_version(const std::string& version);
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_FILE_WRITER_HPP
