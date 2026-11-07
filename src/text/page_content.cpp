// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// A page's faults are listed once each, in the order met, so a message
// repeated by every glyph of a broken font reads as one line; its damage
// names each stream and class once, so a form drawn twice reads as one.

#include "text/page_content.hpp"

#include <algorithm>

namespace pdfkit::text {

void PageContent::fault(std::string message) {
  if (std::ranges::find(faults, message) == faults.end()) {
    faults.push_back(std::move(message));
  }
}

std::optional<syntax::PageDamage> PageContent::damaged_page() const {
  syntax::PageDamage out;
  out.page = page;
  out.glyphs = glyphs.size();
  for (const ContentStream& stream : streams) {
    for (const syntax::PageDamage::Stream& entry : stream.damage) {
      out.add(entry);
    }
  }
  if (out.streams.empty()) {
    return std::nullopt;
  }
  return out;
}

}  // namespace pdfkit::text
