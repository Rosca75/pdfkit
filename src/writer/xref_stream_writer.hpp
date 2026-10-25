// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// XrefStreamWriter writes the one cross-reference section of an output, a
// cross-reference stream of ISO 32000-1 section 7.5.8 (read): /Type /XRef,
// /Size, /Index, /W with one byte for the type, the narrowest width that holds
// the largest offset, and two bytes for the generation field (the free head of
// the list, object 0, carries generation 65535), /Root, /Info and /ID, its
// data Flate-compressed with no predictor; then startxref and %%EOF. The
// stream is the last object: its own entry points at itself. The /ID is
// written as a zero placeholder for DocumentId to patch.

#ifndef PDFKIT_WRITER_XREF_STREAM_WRITER_HPP
#define PDFKIT_WRITER_XREF_STREAM_WRITER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::writer {

class XrefStreamWriter {
 public:
  struct Placed {
    // Where the two /ID placeholders' hexadecimal digits start in out.
    std::size_t id_first = 0;
    std::size_t id_second = 0;
    // The /W written.
    int offset_width = 0;
  };

  // offsets[i] is the byte offset of object i + 1 in out; root and info are
  // output numbers (info absent when no /Info is written). Appends the stream
  // object, startxref and %%EOF to out.
  [[nodiscard]] static Placed write(std::string& out, const std::vector<std::uint64_t>& offsets,
                                    std::uint32_t root, std::optional<std::uint32_t> info);

  // The bytes needed to hold value, at least one.
  [[nodiscard]] static int width_for(std::uint64_t value) noexcept;
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_XREF_STREAM_WRITER_HPP
