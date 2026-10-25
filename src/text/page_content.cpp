// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// A page's faults are listed once each, in the order met, so a message
// repeated by every glyph of a broken font reads as one line.

#include "text/page_content.hpp"

#include <algorithm>

namespace pdfkit::text {

void PageContent::fault(std::string message) {
  if (std::ranges::find(faults, message) == faults.end()) {
    faults.push_back(std::move(message));
  }
}

}  // namespace pdfkit::text
