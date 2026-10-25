// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The mask rules of Masks. /Matte is expressed in the image's colour space;
// the un-premultiplication is applied to the converted grey or RGB samples
// with /Matte converted the same way, which is exact for the device spaces
// /Matte is met with in practice.

#include "images/masks.hpp"

#include <algorithm>
#include <cmath>

namespace pdfkit::images {

using syntax::Dictionary;
using syntax::Document;
using syntax::Object;

Masks::Kind Masks::kind(Document& document, const Dictionary& image, bool jpx) {
  const Object smask = document.resolve_entry(image, "SMask");
  if (smask.stream() != nullptr) {
    return Kind::soft;
  }
  const Object mask = document.resolve_entry(image, "Mask");
  if (mask.stream() != nullptr) {
    return Kind::stencil;
  }
  if (mask.array() != nullptr) {
    return Kind::colour_key;
  }
  if (jpx) {
    const std::int64_t in_data = document.resolve_entry(image, "SMaskInData").integer().value_or(0);
    if (in_data == 1 || in_data == 2) {
      return Kind::in_data;
    }
  }
  return Kind::none;
}

std::string_view Masks::name(Kind kind) noexcept {
  switch (kind) {
    case Kind::none:
      return "none";
    case Kind::soft:
      return "smask";
    case Kind::stencil:
      return "stencil";
    case Kind::colour_key:
      return "color_key";
    case Kind::in_data:
      return "smask_in_data";
  }
  return "none";
}

std::vector<std::uint32_t> Masks::colour_key(Document& document, const Object& mask,
                                             std::size_t components) {
  std::vector<std::uint32_t> ranges;
  const Object value = document.resolve(mask);
  const Object::Array* list = value.array();
  if (list == nullptr || list->size() < 2 * components) {
    return ranges;
  }
  for (std::size_t i = 0; i < 2 * components; ++i) {
    const double number = document.resolve((*list)[i]).number().value_or(-1.0);
    if (number < 0 || number > 65535) {
      return {};
    }
    ranges.push_back(static_cast<std::uint32_t>(std::lround(number)));
  }
  return ranges;
}

bool Masks::keyed(std::span<const std::uint16_t> raw,
                  std::span<const std::uint32_t> ranges) noexcept {
  if (ranges.size() < 2 * raw.size() || raw.empty()) {
    return false;
  }
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] < ranges[2 * i] || raw[i] > ranges[2 * i + 1]) {
      return false;
    }
  }
  return true;
}

void Masks::unmatte(PixelImage& image, const PixelImage& mask, std::span<const double> matte) {
  if (image.width != mask.width || image.height != mask.height || matte.size() < image.channels ||
      image.bits == 1) {
    return;
  }
  const double max = image.bits == 16 ? 65535.0 : 255.0;
  const std::size_t bytes = image.bytes_per_sample();
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const double alpha = mask.sample8(x, y, 0) / 255.0;
      if (alpha <= 0.0) {
        continue;
      }
      for (std::uint32_t c = 0; c < image.channels; ++c) {
        const std::size_t index =
            ((static_cast<std::size_t>(y) * image.width + x) * image.channels + c) * bytes;
        double value = image.samples[index];
        if (bytes == 2) {
          value = value * 256.0 + image.samples[index + 1];
        }
        const double m = std::clamp(matte[c], 0.0, 1.0) * max;
        value = std::clamp(m + (value - m) / alpha, 0.0, max);
        const auto out = static_cast<std::uint32_t>(std::lround(value));
        if (bytes == 2) {
          image.samples[index] = static_cast<std::uint8_t>(out >> 8U);
          image.samples[index + 1] = static_cast<std::uint8_t>(out & 0xFFU);
        } else {
          image.samples[index] = static_cast<std::uint8_t>(out);
        }
      }
    }
  }
}

}  // namespace pdfkit::images
