// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The grouping of a document's image draws by identity. An XObject's identity
// is computed once per object number; an inline image's from its dictionary
// and data each time it is drawn, because two inline images are two copies.
// An occurrence is added in the order of the pages read and of their draws,
// and the occurrences of an entry are kept sorted by page and ordinal, so the
// listing is the same whatever order the pages were asked in.

#include "images/image_listing.hpp"

#include "images/image_catalogue.hpp"
#include "images/image_decoder.hpp"
#include "images/image_identity.hpp"
#include "platform/byte_span.hpp"
#include "platform/error.hpp"
#include "syntax/stream.hpp"
#include "text/page_content.hpp"

#include <algorithm>
#include <utility>

namespace pdfkit::images {

ImageListing::ImageListing(syntax::Document& document)
    : document_(&document), reader_(std::make_unique<text::TextReader>(document)) {}

ImageListing::Entry& ImageListing::entry_for(const std::string& identity) {
  const auto found = by_identity_.find(identity);
  if (found != by_identity_.end()) {
    return entries_[found->second];
  }
  by_identity_.emplace(identity, entries_.size());
  entries_.emplace_back();
  entries_.back().identity = identity;
  return entries_.back();
}

bool ImageListing::may_draw_images(std::uint32_t number) {
  const syntax::Page& page = document_->pages().at(number - 1);
  const syntax::Object resources = document_->resolve(page.resources);
  if (const syntax::Dictionary* dictionary = resources.dictionary()) {
    for (const std::string_view category : {"XObject", "Pattern", "ExtGState"}) {
      const syntax::Object group = document_->resolve_entry(*dictionary, category);
      if (const syntax::Dictionary* entries = group.dictionary();
          entries != nullptr && !entries->empty()) {
        return true;
      }
    }
  }
  // Without XObjects, patterns or graphics states, only an inline image in
  // the page's own content can draw a picture: look for its BI keyword.
  for (const std::vector<std::byte>& piece : document_->content_pieces(page.contents, number)) {
    for (std::size_t i = 0; i + 1 < piece.size(); ++i) {
      if (piece[i] == std::byte{'B'} && piece[i + 1] == std::byte{'I'}) {
        return true;
      }
    }
  }
  return false;
}

void ImageListing::read(const std::vector<std::uint32_t>& pages) {
  for (const std::uint32_t number : pages) {
    if (!read_.insert(number).second) {
      continue;
    }
    // A page that can draw no image is not interpreted, which is most of
    // the cost of a text-only document.
    if (!may_draw_images(number)) {
      continue;
    }
    const text::PageContent content = reader_->interpret(number);
    for (const text::ImageOccurrence& draw : content.images) {
      std::string identity;
      const bool inline_image = draw.kind == text::ImageOccurrence::Kind::inline_image;
      if (inline_image) {
        const syntax::Dictionary* dictionary = draw.dictionary.dictionary();
        if (dictionary == nullptr || !draw.data) {
          continue;
        }
        identity =
            ImageIdentity::of_inline(*document_, *dictionary, platform::ByteSpan(*draw.data));
      } else {
        const syntax::Stream* stream = draw.image.stream();
        if (stream == nullptr) {
          continue;
        }
        const std::uint32_t number_of = draw.reference.number;
        if (number_of != 0) {
          auto known = identity_of_object_.find(number_of);
          if (known == identity_of_object_.end()) {
            known = identity_of_object_
                        .emplace(number_of, ImageIdentity::of_stream(*document_, *stream))
                        .first;
          }
          identity = known->second;
        } else {
          identity = ImageIdentity::of_stream(*document_, *stream);
        }
      }
      Entry& entry = entry_for(identity);
      if (entry.occurrences.empty() && entry.pattern_pages.empty()) {
        if (inline_image) {
          entry.source = Source::inline_image;
          entry.dictionary = draw.dictionary;
          entry.colour_spaces = draw.colour_spaces;
          entry.data = draw.data;
        } else {
          entry.source = Source::xobject;
          entry.stream = draw.image;
          entry.object_number = draw.reference.number;
        }
      }
      entry.occurrences.push_back(
          {draw.page, draw.ordinal, draw.box, draw.form_path, draw.optional_content_off, draw.ctm});
    }
    for (const text::PageContent::Unwalked& unwalked : content.unwalked) {
      if (unwalked.resources.is_null()) {
        continue;
      }
      const ImageCatalogue inside(*document_, unwalked.resources, number);
      for (const ImageCatalogue::Entry& found : inside.entries()) {
        Entry& entry = entry_for(found.identity);
        if (entry.occurrences.empty() && entry.pattern_pages.empty()) {
          entry.source = Source::xobject;
          entry.stream = found.stream;
          entry.object_number = found.object_number;
        }
        if (std::ranges::find(entry.pattern_pages, number) == entry.pattern_pages.end()) {
          entry.pattern_pages.push_back(number);
          std::ranges::sort(entry.pattern_pages);
        }
      }
    }
  }
  // Pages may be read in any order; the draws of an entry are kept in page
  // order, then draw order, sorted once per call.
  for (Entry& entry : entries_) {
    std::ranges::stable_sort(entry.occurrences, [](const Occurrence& a, const Occurrence& b) {
      return a.page != b.page ? a.page < b.page : a.ordinal < b.ordinal;
    });
  }
}

ImageListing::Entry* ImageListing::find(std::string_view identity) {
  const auto found = by_identity_.find(identity);
  return found != by_identity_.end() ? &entries_[found->second] : nullptr;
}

void ImageListing::probe(Entry& entry) {
  if (entry.probed) {
    return;
  }
  try {
    ImageDecoder::Result result;
    if (entry.source == Source::inline_image) {
      result = ImageDecoder::decode_inline(*document_, *entry.dictionary.dictionary(),
                                           platform::ByteSpan(*entry.data), entry.colour_spaces,
                                           ImageDecoder::Part::base);
    } else {
      result = ImageDecoder::decode(*document_, *entry.stream.stream(), ImageDecoder::Part::base);
    }
    entry.fault = result.image ? std::string() : result.fault;
    if (result.image) {
      entry.width = result.image->width;
      entry.height = result.image->height;
      entry.bitonal = result.image->bits == 1 && result.image->channels == 1;
    }
  } catch (const platform::Error& error) {
    if (error.code() != platform::ErrorCode::limit_exceeded) {
      throw;
    }
    entry.fault = error.failed();
  }
  entry.probed = true;
}

}  // namespace pdfkit::images
