// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The stream rules of the writer and the deflate wrapper over zlib. A /Crypt
// entry is removed from the filter chain together with its /DecodeParms entry,
// so the remaining parameters stay aligned with their filters.

#include "writer/stream_copier.hpp"

#include "platform/error.hpp"
#include "syntax/object.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

namespace pdfkit::writer {

using syntax::Dictionary;
using syntax::Object;

namespace {

// The dictionary with /Length set to length, in its place when present.
void set_length(Dictionary& dictionary, std::size_t length) {
  dictionary.set("Length", Object::make_integer(static_cast<std::int64_t>(length)));
}

// Removes /Crypt from the filter chain of dictionary.
void drop_crypt(Dictionary& dictionary) {
  const Object& filter = dictionary.get("Filter");
  if (filter.is_name("Crypt")) {
    dictionary.erase("Filter");
    dictionary.erase("DecodeParms");
    return;
  }
  const Object::Array* filters = filter.array();
  if (filters == nullptr) {
    return;
  }
  const Object::Array* parameters = dictionary.get("DecodeParms").array();
  Object::Array kept_filters;
  Object::Array kept_parameters;
  bool removed = false;
  for (std::size_t i = 0; i < filters->size(); ++i) {
    if ((*filters)[i].is_name("Crypt")) {
      removed = true;
      continue;
    }
    kept_filters.push_back((*filters)[i]);
    if (parameters != nullptr) {
      kept_parameters.push_back(i < parameters->size() ? (*parameters)[i] : Object());
    }
  }
  if (!removed) {
    return;
  }
  if (kept_filters.empty()) {
    dictionary.erase("Filter");
    dictionary.erase("DecodeParms");
    return;
  }
  dictionary.set("Filter", Object::make_array(std::move(kept_filters)));
  if (parameters != nullptr) {
    dictionary.set("DecodeParms", Object::make_array(std::move(kept_parameters)));
  }
}

}  // namespace

StreamCopier::Copy StreamCopier::copy(const syntax::Document& document,
                                      const syntax::Stream& stream) {
  Copy copy{stream.dictionary(), {}};
  const platform::ByteSpan raw = document.raw(stream);
  if (!stream.owns_data() && document.security()) {
    copy.data = document.security()->decrypt_stream(stream.owner(), stream.dictionary(), raw);
    drop_crypt(copy.dictionary);
  } else {
    copy.data.assign(raw.begin(), raw.end());
  }
  set_length(copy.dictionary, copy.data.size());
  return copy;
}

syntax::Stream StreamCopier::compress(Dictionary dictionary, platform::ByteSpan decoded) {
  dictionary.erase("DecodeParms");
  dictionary.erase("DL");
  dictionary.erase("Length");
  dictionary.set("Filter", Object::make_name("FlateDecode"));
  return {std::move(dictionary), deflate(decoded)};
}

syntax::Stream StreamCopier::plain(Dictionary dictionary, platform::ByteSpan data) {
  dictionary.erase("Filter");
  dictionary.erase("DecodeParms");
  dictionary.erase("DL");
  dictionary.erase("Length");
  return {std::move(dictionary), std::vector<std::byte>(data.begin(), data.end())};
}

std::vector<std::byte> StreamCopier::deflate(platform::ByteSpan data) {
  z_stream stream{};
  // windowBits 15 and memLevel 8 are zlib's defaults (zlib manual, recalled).
  if (deflateInit2(&stream, compression_level, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    throw platform::Error(platform::ErrorCode::internal, "zlib could not start deflating",
                          "zlib to start a deflate stream",
                          "this is a defect in pdfkit or an out-of-memory condition; report it");
  }
  std::vector<std::byte> out;
  std::vector<unsigned char> chunk(65536);
  std::size_t fed = 0;
  int result = Z_OK;
  try {
    do {
      if (stream.avail_in == 0 && fed < data.size()) {
        const std::size_t piece = std::min<std::size_t>(data.size() - fed, 1U << 30U);
        // zlib takes a non-const pointer and never writes through it.
        stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data() + fed));
        stream.avail_in = static_cast<uInt>(piece);
        fed += piece;
      }
      stream.next_out = chunk.data();
      stream.avail_out = static_cast<uInt>(chunk.size());
      result = ::deflate(&stream, fed >= data.size() ? Z_FINISH : Z_NO_FLUSH);
      const auto produced = std::as_bytes(std::span(chunk.data(), chunk.size() - stream.avail_out));
      out.insert(out.end(), produced.begin(), produced.end());
    } while (result == Z_OK || result == Z_BUF_ERROR);
  } catch (...) {
    deflateEnd(&stream);
    throw;
  }
  deflateEnd(&stream);
  if (result != Z_STREAM_END) {
    throw platform::Error(platform::ErrorCode::internal, "zlib failed while deflating a stream",
                          "zlib to compress the stream to its end",
                          "this is a defect in pdfkit; report it with the input file");
  }
  return out;
}

}  // namespace pdfkit::writer
