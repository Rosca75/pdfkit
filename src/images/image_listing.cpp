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

void ImageListing::read(const std::vector<std::uint32_t>& pages) {
  for (const std::uint32_t number : pages) {
    if (!read_.insert(number).second) {
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
          {draw.page, draw.ordinal, draw.box, draw.form_path, draw.optional_content_off});
      std::ranges::stable_sort(entry.occurrences, [](const Occurrence& a, const Occurrence& b) {
        return a.page != b.page ? a.page < b.page : a.ordinal < b.ordinal;
      });
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
  } catch (const platform::Error& error) {
    if (error.code() != platform::ErrorCode::limit_exceeded) {
      throw;
    }
    entry.fault = error.failed();
  }
  entry.probed = true;
}

}  // namespace pdfkit::images
