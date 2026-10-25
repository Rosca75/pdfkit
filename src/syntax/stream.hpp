// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Stream is a PDF stream object of ISO 32000-1 section 7.3.8 (read) as the
// parser found it: its dictionary, where its raw data lies in the document's
// bytes, and the indirect object that holds it. The data is never copied at
// parse time; Document decodes it on demand through the filters and the
// decoding budget, and the owner is what the security handler derives the
// stream's key from and what a message names.

#ifndef PDFKIT_SYNTAX_STREAM_HPP
#define PDFKIT_SYNTAX_STREAM_HPP

#include "syntax/dictionary.hpp"
#include "syntax/object.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace pdfkit::syntax {

class Stream {
 public:
  Stream(Dictionary dictionary, std::size_t data_offset, std::size_t data_length,
         Object::Reference owner)
      : dictionary_(std::move(dictionary)),
        data_offset_(data_offset),
        data_length_(data_length),
        owner_(owner) {}

  [[nodiscard]] const Dictionary& dictionary() const noexcept { return dictionary_; }
  // The raw data's position and length in the document's bytes (after the
  // header offset), as the length rule of docs/DESIGN.md section 1.3 fixed it.
  [[nodiscard]] std::size_t data_offset() const noexcept { return data_offset_; }
  [[nodiscard]] std::size_t data_length() const noexcept { return data_length_; }
  [[nodiscard]] Object::Reference owner() const noexcept { return owner_; }

  // "object 12 0", for messages.
  [[nodiscard]] std::string describe() const;

  friend bool operator==(const Stream& a, const Stream& b) = default;

 private:
  Dictionary dictionary_;
  std::size_t data_offset_;
  std::size_t data_length_;
  Object::Reference owner_;
};

}  // namespace pdfkit::syntax

#endif  // PDFKIT_SYNTAX_STREAM_HPP
