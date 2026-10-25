// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageTreatmentPlan is the image part of the edit plan (CLAUDE.md section 4,
// treatment; docs/DESIGN.md section 14): per image identity one treatment,
// box, blur or remove, and any number of region boxes, each a box over a
// rectangle of the image the caller found in its pixels. add() checks every
// request before it records any, so a refused call leaves the plan as it was:
// an identity no page draws, a second box, blur or remove of one identity
// (refused naming the first), a box or blur of an image that does not decode
// (remove is offered), a blur of a bitonal image too small to hide its shapes
// (box is offered), a caption outside the placeholder alphabet, a caption on
// anything but a box, a region of a removed image, a rectangle that covers no
// pixel, a page rectangle on a page that does not draw the image. A page
// rectangle is mapped onto the pixels through the inverse of the CTM of each
// draw of the image on that page, and every mapped rectangle is treated. The
// plan keeps, per identity, the pixel rectangles to paint and to mosaic, the
// caption of each box, the image's size and the device rectangles each page's
// draws change, which the write reports.

#ifndef PDFKIT_REDACT_IMAGE_TREATMENT_PLAN_HPP
#define PDFKIT_REDACT_IMAGE_TREATMENT_PLAN_HPP

#include "images/image_listing.hpp"
#include "images/treatments/box_painter.hpp"
#include "images/treatments/caption_drawer.hpp"
#include "images/treatments/pixel_buffer.hpp"
#include "syntax/document.hpp"
#include "text/rect.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pdfkit::redact {

class ImageTreatmentPlan {
 public:
  using PixelRect = images::treatments::PixelBuffer::Rect;
  using Colour = images::treatments::BoxPainter::Colour;

  enum class Kind : std::uint8_t { box, blur, remove, region };

  // One request of pdfkit_redact_images.
  struct Request {
    std::string identity;
    Kind kind = Kind::box;
    // [left, top, right, bottom] in pixels, or [left, bottom, right, top] in
    // page space; absent: the whole image.
    std::optional<std::array<double, 4>> rectangle;
    bool page_space = false;
    std::uint32_t page = 0;
    std::optional<std::string> caption;
    std::optional<Colour> colour;
  };

  // A box painted into the pixels.
  struct Box {
    PixelRect pixels;
    Colour colour{0.0, 0.0, 0.0};
    std::string caption;
  };

  struct Treatment {
    // The identity's one treatment; absent when it holds region boxes only.
    std::optional<Kind> kind;
    // The request that gave it, for a refusal ("treatment 2 of this call").
    std::string given_by;
    bool inline_image = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<Box> boxes;
    std::vector<PixelRect> blurs;
    std::uint32_t block = 0;
    // The orientation a caption is drawn in: the first draw's.
    images::treatments::CaptionDrawer::Orientation orientation;
    // Per source page, the device rectangles its draws change.
    std::map<std::uint32_t, std::vector<text::Rect>> rectangles;
  };

  // What one request reaches.
  struct Answer {
    std::string identity;
    Kind kind = Kind::box;
    std::vector<PixelRect> pixels;
    std::uint32_t block = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    struct Draw {
      std::uint32_t page = 0;
      std::size_t ordinal = 0;
      std::vector<text::Rect> rectangles;
    };
    std::vector<Draw> occurrences;
    // The SHA-256 (hexadecimal) and length of the image's encoded bytes.
    std::string encoded_sha256;
    std::size_t encoded_bytes = 0;
  };

  // Checks every request, then records them; throws platform::Error with
  // the code of the first refusal. call names the call in messages.
  std::vector<Answer> add(syntax::Document& document, images::ImageListing& listing,
                          const std::vector<Request>& requests);

  [[nodiscard]] const std::map<std::string, Treatment>& treatments() const noexcept {
    return treatments_;
  }
  [[nodiscard]] bool empty() const noexcept { return treatments_.empty(); }

  // The name of a kind: "box", "blur", "remove", "region".
  [[nodiscard]] static std::string_view name(Kind kind) noexcept;

 private:
  std::map<std::string, Treatment> treatments_;
  std::size_t calls_ = 0;
};

}  // namespace pdfkit::redact

#endif  // PDFKIT_REDACT_IMAGE_TREATMENT_PLAN_HPP
