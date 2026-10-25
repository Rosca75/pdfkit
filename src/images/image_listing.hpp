// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageListing is a document's images as pages draw them, docs/DESIGN.md
// sections 9.1 and 9.2: the content interpreter (text::TextReader, the one
// walker of CLAUDE.md section 3.2) gives each page's image draws in order,
// through forms, and the listing groups them by image identity, so a picture
// drawn on three pages and in a form is one entry with four occurrences, and
// an inline image and an XObject of the same bytes share an entry. An image
// reached only inside a tiling pattern or a soft-mask group, which the
// interpreter does not walk, is listed with the pages that set it and no
// occurrence (the image_in_pattern case of chapter 19). Pages are read on
// demand and once; whether an entry decodes is probed once and kept, a limit
// of the decoding budget met by the probe being recorded as the image's fault
// so one oversized image does not hide the others.

#ifndef PDFKIT_IMAGES_IMAGE_LISTING_HPP
#define PDFKIT_IMAGES_IMAGE_LISTING_HPP

#include "syntax/document.hpp"
#include "syntax/object.hpp"
#include "text/matrix.hpp"
#include "text/rect.hpp"
#include "text/text_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::images {

class ImageListing {
 public:
  struct Occurrence {
    std::uint32_t page = 0;
    std::size_t ordinal = 0;
    text::Rect box;
    std::vector<std::uint32_t> form_path;
    bool optional_content_off = false;
    // The CTM at the draw: the image is the unit square under it, which is
    // how a treatment maps a page rectangle onto the image's pixels.
    text::Matrix ctm;
  };

  enum class Source : std::uint8_t { xobject, inline_image };

  struct Entry {
    std::string identity;
    Source source = Source::xobject;
    // An XObject: its stream and object number (0 for a direct stream).
    syntax::Object stream;
    std::uint32_t object_number = 0;
    // An inline image: its dictionary as written, the /ColorSpace resources
    // of the content that drew it, and its data.
    syntax::Object dictionary;
    syntax::Object colour_spaces;
    std::shared_ptr<const std::vector<std::byte>> data;
    // Draws in page order, then draw order.
    std::vector<Occurrence> occurrences;
    // The pages on which it is reached only inside a pattern or a soft mask.
    std::vector<std::uint32_t> pattern_pages;
    // Set by probe: whether it was probed and why it does not decode.
    bool probed = false;
    std::string fault;
  };

  // document must outlive the listing.
  explicit ImageListing(syntax::Document& document);

  // Interprets every page of pages (1-based, within the document) not read
  // yet.
  void read(const std::vector<std::uint32_t>& pages);

  [[nodiscard]] const std::deque<Entry>& entries() const noexcept { return entries_; }
  // The entry of an identity among the pages read, or nullptr.
  [[nodiscard]] Entry* find(std::string_view identity);

  // Decodes the entry's base once and records its fault (empty when it
  // decodes).
  void probe(Entry& entry);

 private:
  Entry& entry_for(const std::string& identity);
  // Whether page number can draw an image at all: its resources name an
  // XObject, a pattern or a graphics state, or its content holds "BI". A page
  // that cannot is not interpreted.
  [[nodiscard]] bool may_draw_images(std::uint32_t number);

  syntax::Document* document_;
  std::unique_ptr<text::TextReader> reader_;
  std::set<std::uint32_t> read_;
  // A deque, so an entry's address survives later pages.
  std::deque<Entry> entries_;
  std::map<std::string, std::size_t, std::less<>> by_identity_;
  std::map<std::uint32_t, std::string> identity_of_object_;
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_IMAGE_LISTING_HPP
