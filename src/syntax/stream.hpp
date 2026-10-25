// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Stream is a PDF stream object of ISO 32000-1 section 7.3.8 (read) in one of
// two forms. A stream the parser found holds its dictionary, where its raw
// data lies in the document's bytes, and the indirect object that holds it;
// the data is never copied at parse time, Document decodes it on demand
// through the filters and the decoding budget, and the owner is what the
// security handler derives the stream's key from and what a message names. A
// stream the writer creates holds its encoded data itself (shared, so a copy
// stays cheap), described by its dictionary, and is never decrypted.

#ifndef PDFKIT_SYNTAX_STREAM_HPP
#define PDFKIT_SYNTAX_STREAM_HPP

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::syntax {

class Stream {
 public:
  // A stream as read from a document.
  Stream(Dictionary dictionary, std::size_t data_offset, std::size_t data_length,
         Object::Reference owner)
      : dictionary_(std::move(dictionary)),
        data_offset_(data_offset),
        data_length_(data_length),
        owner_(owner) {}

  // A stream created in memory: data is its encoded bytes, as its dictionary's
  // /Filter describes them.
  Stream(Dictionary dictionary, std::vector<std::byte> data)
      : dictionary_(std::move(dictionary)),
        owned_(std::make_shared<const std::vector<std::byte>>(std::move(data))) {}

  [[nodiscard]] const Dictionary& dictionary() const noexcept { return dictionary_; }
  // The raw data's position and length in the document's bytes (after the
  // header offset), as the length rule of docs/DESIGN.md section 1.3 fixed it;
  // zero for a created stream.
  [[nodiscard]] std::size_t data_offset() const noexcept { return data_offset_; }
  [[nodiscard]] std::size_t data_length() const noexcept { return data_length_; }
  [[nodiscard]] Object::Reference owner() const noexcept { return owner_; }

  // Whether the stream holds its data itself (a created stream).
  [[nodiscard]] bool owns_data() const noexcept { return owned_ != nullptr; }
  // The data of a created stream; empty for a stream read from a document.
  [[nodiscard]] platform::ByteSpan owned_data() const noexcept {
    return owned_ != nullptr ? platform::ByteSpan(*owned_) : platform::ByteSpan();
  }

  // The same stream, data and owner under another dictionary.
  [[nodiscard]] Stream with_dictionary(Dictionary dictionary) const;

  // "object 12 0", for messages.
  [[nodiscard]] std::string describe() const;

  // Equal dictionaries, owners and ranges, and equal owned bytes.
  friend bool operator==(const Stream& a, const Stream& b);

 private:
  Dictionary dictionary_;
  std::size_t data_offset_ = 0;
  std::size_t data_length_ = 0;
  Object::Reference owner_;
  std::shared_ptr<const std::vector<std::byte>> owned_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_STREAM_HPP
