// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The charged growth of platform::Buffer: the budget is charged before the
// vector grows, so a refused append leaves the buffer as it was.

#include "platform/buffer.hpp"

#include <utility>

namespace pdfkit::platform {

Buffer::Buffer(Budget& budget, std::string object) : budget_(&budget), object_(std::move(object)) {}

void Buffer::append(ByteSpan bytes) {
  budget_->charge(bytes.size(), stream_used_, object_);
  bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

void Buffer::push_back(std::byte value) {
  budget_->charge(1, stream_used_, object_);
  bytes_.push_back(value);
}

void Buffer::reserve(std::size_t capacity) {
  budget_->check_claim(capacity, object_);
  bytes_.reserve(capacity);
}

}  // namespace pdfkit::platform
