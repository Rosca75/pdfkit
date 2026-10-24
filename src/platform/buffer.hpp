// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Buffer is an owned, growable byte vector whose growth is charged to a Budget
// as one stream, so the output of a decoder can never outgrow the limits of
// its document. Reserving checks the claim without charging it; only bytes
// actually appended count.

#ifndef PDFKIT_PLATFORM_BUFFER_HPP
#define PDFKIT_PLATFORM_BUFFER_HPP

#include "platform/budget.hpp"
#include "platform/byte_span.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::platform {

class Buffer {
 public:
  // budget must outlive the buffer. object names the stream in error messages.
  Buffer(Budget& budget, std::string object);

  void append(ByteSpan bytes);
  void push_back(std::byte value);
  void reserve(std::size_t capacity);

  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }
  [[nodiscard]] ByteSpan view() const noexcept { return bytes_; }
  [[nodiscard]] std::vector<std::byte> release() && { return std::move(bytes_); }

 private:
  Budget* budget_;
  std::string object_;
  std::uint64_t stream_used_ = 0;
  std::vector<std::byte> bytes_;
};

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_BUFFER_HPP
