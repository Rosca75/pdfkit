// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The q/Q stack with its bound. A q past the bound is remembered only as a
// count, so its Q pops the count and leaves the state as it is.

#include "text/graphics_state.hpp"

namespace pdfkit::text {

bool GraphicsState::save() {
  if (stack_.size() >= max_depth) {
    ++overflow_;
    return false;
  }
  stack_.push_back(current_);
  return true;
}

bool GraphicsState::restore() {
  if (overflow_ > 0) {
    --overflow_;
    return true;
  }
  if (stack_.empty()) {
    return false;
  }
  current_ = std::move(stack_.back());
  stack_.pop_back();
  return true;
}

}  // namespace pdfkit::text
