// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// StreamCopier decides the bytes and the dictionary a stream is written with,
// docs/DESIGN.md section 17. An untouched stream keeps its /Filter,
// /DecodeParms and encoded bytes; from an encrypted source it is written
// decrypted with its filters kept, less a /Crypt filter (ISO 32000-1 section
// 7.4.10, read), because the output is never encrypted and a /Crypt naming a
// filter the output has no /Encrypt for would not decode. A created stream is
// written as its creator encoded it; compress() encodes a rewritten stream
// with Flate at a fixed zlib level, so the zlib version and the level (both
// pinned) make the bytes. /Length is written as a direct integer, and /DL is
// dropped from a stream whose bytes the writer re-encodes.

#ifndef PDFKIT_WRITER_STREAM_COPIER_HPP
#define PDFKIT_WRITER_STREAM_COPIER_HPP

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/stream.hpp"

#include <cstddef>
#include <vector>

namespace pdfkit::writer {

class StreamCopier {
 public:
  // zlib's level 6, its default trade of speed for size (zlib manual,
  // recalled).
  static constexpr int compression_level = 6;

  struct Copy {
    syntax::Dictionary dictionary;
    std::vector<std::byte> data;
  };

  // The dictionary (with its direct /Length) and the bytes stream is written
  // with; document holds a stream read from it.
  [[nodiscard]] static Copy copy(const syntax::Document& document, const syntax::Stream& stream);

  // A created stream holding decoded Flate-compressed under dictionary, whose
  // /Filter becomes /FlateDecode and whose /DecodeParms, /DL and /Length go.
  [[nodiscard]] static syntax::Stream compress(syntax::Dictionary dictionary,
                                               platform::ByteSpan decoded);

  // A created stream holding data as it is, uncompressed (the XMP packet).
  [[nodiscard]] static syntax::Stream plain(syntax::Dictionary dictionary, platform::ByteSpan data);

  // data as a zlib stream (RFC 1950, recalled) at compression_level.
  [[nodiscard]] static std::vector<std::byte> deflate(platform::ByteSpan data);
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_STREAM_COPIER_HPP
