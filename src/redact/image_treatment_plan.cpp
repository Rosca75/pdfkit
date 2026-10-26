// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The checks of pdfkit_redact_images and what each request reaches. The
// requests work on a copy of the plan, which replaces the plan only when
// every request passed. Each refusal names the request by its index in the
// call (and the call, after the first), what it asked, what was expected and
// what to do instead.

#include "redact/image_treatment_plan.hpp"

#include "images/image_decoder.hpp"
#include "images/image_identity.hpp"
#include "images/treatments/mosaic_blur.hpp"
#include "images/treatments/region_mapper.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"

#include <cmath>
#include <numeric>
#include <utility>

namespace pdfkit::redact {

namespace {

using images::treatments::CaptionDrawer;
using images::treatments::MosaicBlur;
using images::treatments::RegionMapper;
using platform::Error;
using platform::ErrorCode;

std::string article(ImageTreatmentPlan::Kind kind) {
  if (kind == ImageTreatmentPlan::Kind::region) {
    return "a region box";
  }
  return "a " + std::string(ImageTreatmentPlan::name(kind));
}

[[noreturn]] void refuse(const std::string& failed, const std::string& expected,
                         const std::string& fix) {
  throw Error(ErrorCode::invalid_argument, "pdfkit_redact_images " + failed, expected, fix);
}

std::string hex(std::span<const std::byte> digest) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (const std::byte value : digest) {
    const auto byte = std::to_integer<unsigned>(value);
    out += digits[byte >> 4U];
    out += digits[byte & 0x0FU];
  }
  return out;
}

// The caption's orientation: the pixel steps that move one caption pixel
// right (+x on the page) and one down (-y), snapped to the image's axes.
CaptionDrawer::Orientation orientation_of(const text::Matrix& ctm) {
  CaptionDrawer::Orientation out;
  if (std::abs(ctm.a) >= std::abs(ctm.b)) {
    // Columns run across the page: a column step moves x by a; a row step
    // moves y by -d.
    out.right_x = ctm.a >= 0 ? 1 : -1;
    out.right_y = 0;
    out.down_x = 0;
    out.down_y = ctm.d >= 0 ? 1 : -1;
  } else {
    // Columns run up or down the page: a column step moves y by b; a row
    // step moves x by -c.
    out.right_x = 0;
    out.right_y = ctm.c <= 0 ? 1 : -1;
    out.down_x = ctm.b <= 0 ? 1 : -1;
    out.down_y = 0;
  }
  return out;
}

}  // namespace

std::string_view ImageTreatmentPlan::name(Kind kind) noexcept {
  switch (kind) {
    case Kind::box:
      return "box";
    case Kind::blur:
      return "blur";
    case Kind::remove:
      return "remove";
    case Kind::region:
      return "region";
  }
  return "box";
}

std::vector<ImageTreatmentPlan::Answer> ImageTreatmentPlan::add(
    syntax::Document& document, images::ImageListing& listing,
    const std::vector<Request>& requests) {
  ++calls_;
  const std::string call = calls_ > 1 ? " of call " + std::to_string(calls_) : std::string();
  std::vector<std::uint32_t> pages(document.pages().size());
  std::iota(pages.begin(), pages.end(), 1U);
  listing.read(pages);
  std::map<std::string, Treatment> next = treatments_;
  std::vector<Answer> answers;
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const Request& request = requests[i];
    const std::string where = "treatment " + std::to_string(i);
    images::ImageListing::Entry* entry = listing.find(request.identity);
    if (entry == nullptr) {
      refuse(
          where + " names the image " + request.identity + ", which no page of this document draws",
          "an identity that pdfkit_images lists for this document",
          "take the identity from pdfkit_images of this document");
    }
    Treatment& treatment = next[request.identity];
    treatment.inline_image = entry->source == images::ImageListing::Source::inline_image;
    const bool remove = request.kind == Kind::remove;
    if (!remove) {
      listing.probe(*entry);
      if (!entry->fault.empty()) {
        throw Error(ErrorCode::undecodable_image,
                    "pdfkit_redact_images " + where + " asks for " + article(request.kind) +
                        " over the image " + request.identity +
                        ", which does not decode: " + entry->fault,
                    "an image pdfkit decodes, for a box or a blur",
                    "give this image the treatment \"remove\", which needs no decoding");
      }
      treatment.width = entry->width;
      treatment.height = entry->height;
    }
    if (request.kind != Kind::region) {
      if (treatment.kind) {
        refuse(where + " gives the image " + request.identity + " " + article(request.kind) +
                   ", but " + treatment.given_by + " gave it " + article(*treatment.kind),
               "one box, blur or remove per image; region boxes may be added beside a box or a "
               "blur",
               "keep one of the two treatments of this image");
      }
      if (remove && !treatment.boxes.empty()) {
        refuse(where + " removes the image " + request.identity + ", which " + treatment.given_by +
                   " gave region boxes",
               "region boxes only on an image that is kept", "drop the region boxes or the remove");
      }
      treatment.kind = request.kind;
      treatment.given_by = where + call;
    } else if (treatment.kind == Kind::remove) {
      refuse(where + " adds a region box to the image " + request.identity + ", which " +
                 treatment.given_by + " removes",
             "region boxes only on an image that is kept", "drop the region box or the remove");
    } else if (treatment.given_by.empty()) {
      treatment.given_by = where + call;
    }
    const bool boxes = request.kind == Kind::box || request.kind == Kind::region;
    if (request.caption && !boxes) {
      refuse(
          where + " gives " + article(request.kind) + " the caption \"" + *request.caption + "\"",
          "a caption on a box or a region box only", "drop the caption or use a box");
    }
    if (request.colour && !boxes) {
      refuse(where + " gives " + article(request.kind) + " a colour",
             "a colour on a box or a region box only", "drop the colour or use a box");
    }
    if (request.caption) {
      if (const std::string bad = CaptionDrawer::outside(*request.caption); !bad.empty()) {
        refuse(where + " has the caption \"" + *request.caption + "\", which holds '" +
                   bad.substr(0, 1) + "', a character outside the placeholder alphabet",
               "a caption of the characters [, ], _, 0 to 9 and A to Z, which the caption font "
               "draws",
               "write the caption in capitals, digits, brackets and underscores, for example "
               "REDACTED");
      }
    }
    // A JPEG is written as a JPEG again, so its mosaic keeps to JPEG blocks.
    const syntax::Object* written = treatment.inline_image ? &entry->dictionary : &entry->stream;
    const syntax::Dictionary* fields = written->dictionary_or_stream_dictionary();
    const bool jpeg =
        fields != nullptr && images::ImageDecoder::encoding(document, *fields) == "DCTDecode";
    if (request.kind == Kind::blur) {
      MosaicBlur::require_blurrable(entry->bitonal, treatment.width, treatment.height,
                                    "pdfkit_redact_images " + where);
    }
    if (remove && request.rectangle) {
      refuse(where + " gives a remove a rectangle", "a remove of the whole image",
             "drop the rectangle, or use a box over it");
    }
    // The pixel rectangles the request treats.
    std::vector<PixelRect> rects;
    if (!remove) {
      if (!request.rectangle) {
        rects.push_back({0, 0, treatment.width, treatment.height});
      } else if (!request.page_space) {
        if (const auto clipped =
                RegionMapper::clip(*request.rectangle, treatment.width, treatment.height)) {
          rects.push_back(*clipped);
        }
      } else {
        const auto& r = *request.rectangle;
        bool drawn = false;
        for (const images::ImageListing::Occurrence& draw : entry->occurrences) {
          if (draw.page != request.page) {
            continue;
          }
          drawn = true;
          const auto mapped = RegionMapper::to_pixels(draw.ctm, treatment.width, treatment.height,
                                                      text::Rect::of(r[0], r[1], r[2], r[3]));
          if (mapped && std::ranges::find(rects, *mapped) == rects.end()) {
            rects.push_back(*mapped);
          }
        }
        if (!drawn) {
          refuse(where + " gives a page rectangle on page " + std::to_string(request.page) +
                     ", which does not draw the image " + request.identity,
                 "a page that draws the image (its occurrences in pdfkit_images)",
                 "name a page the image is drawn on, or give the rectangle in pixels");
        }
      }
      if (rects.empty()) {
        refuse(where + " gives a rectangle that covers none of the pixels of the image " +
                   request.identity + " (" + std::to_string(treatment.width) + " by " +
                   std::to_string(treatment.height) + ")",
               "a rectangle over the image: in pixels [left, top, right, bottom] from its "
               "top-left corner, or on a page that draws it",
               "check the rectangle's space and corners");
      }
    }
    Answer answer;
    answer.identity = request.identity;
    answer.kind = request.kind;
    answer.pixels = rects;
    answer.width = treatment.width;
    answer.height = treatment.height;
    if (request.kind == Kind::blur) {
      treatment.blurs = rects;
      treatment.block = MosaicBlur::block_size(treatment.width, treatment.height, jpeg);
      answer.block = treatment.block;
    } else if (boxes) {
      for (const PixelRect& rect : rects) {
        treatment.boxes.push_back(
            {rect, request.colour.value_or(Colour{0.0, 0.0, 0.0}), request.caption.value_or("")});
      }
    }
    if (!entry->occurrences.empty()) {
      treatment.orientation = orientation_of(entry->occurrences.front().ctm);
    }
    for (const images::ImageListing::Occurrence& draw : entry->occurrences) {
      Answer::Draw reached{draw.page, draw.ordinal, {}};
      if (remove) {
        reached.rectangles.push_back(draw.box);
      } else {
        for (const PixelRect& rect : rects) {
          reached.rectangles.push_back(
              RegionMapper::to_page(draw.ctm, treatment.width, treatment.height, rect));
        }
      }
      auto& page = treatment.rectangles[draw.page];
      page.insert(page.end(), reached.rectangles.begin(), reached.rectangles.end());
      answer.occurrences.push_back(std::move(reached));
    }
    // An image a page reaches only inside a tiling pattern or a soft-mask
    // group is drawn wherever the pattern paints, which no occurrence names:
    // the whole media box of each such page is what the treatment may change.
    for (const std::uint32_t number : entry->pattern_pages) {
      if (number >= 1 && number <= document.pages().size()) {
        const syntax::Page::Box& media = document.pages()[number - 1].media_box;
        auto& page = treatment.rectangles[number];
        const text::Rect whole = text::Rect::of(media.left, media.bottom, media.right, media.top);
        if (std::ranges::find(page, whole) == page.end()) {
          page.push_back(whole);
        }
      }
    }
    std::vector<std::byte> encoded;
    if (treatment.inline_image) {
      if (entry->data) {
        encoded = *entry->data;
      }
    } else if (const syntax::Stream* stream = entry->stream.stream()) {
      encoded = images::ImageIdentity::encoded_bytes(document, *stream);
    }
    answer.encoded_bytes = encoded.size();
    answer.encoded_sha256 = hex(platform::crypto::Sha2::digest(
        platform::crypto::Sha2::Variant::sha256, platform::ByteSpan(encoded)));
    answers.push_back(std::move(answer));
  }
  treatments_ = std::move(next);
  return answers;
}

}  // namespace pdfkit::redact
