// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The description of an image dictionary. A key is found under its full or
// its abbreviated name (the last written wins, as in a dictionary); a filter
// name is reported by its full name.

#include "images/image_info.hpp"

#include "images/colour_space.hpp"
#include "images/decode_fault.hpp"
#include "images/image_identity.hpp"
#include "images/masks.hpp"
#include "syntax/filters.hpp"

#include <cmath>
#include <string_view>

namespace pdfkit::images {

namespace {

syntax::Object entry(syntax::Document& document, const syntax::Dictionary& dictionary,
                     std::string_view key) {
  const syntax::Object* found = nullptr;
  for (const auto& [written, value] : dictionary.entries()) {
    if (ImageIdentity::full_key(written) == key) {
      found = &value;
    }
  }
  return found != nullptr ? document.resolve(*found) : syntax::Object();
}

std::optional<std::int64_t> whole(const syntax::Object& value) {
  if (const std::optional<std::int64_t> integer = value.integer()) {
    return integer;
  }
  if (const std::optional<double> real = value.number();
      real && std::isfinite(*real) && std::fabs(*real) < 9.0e15) {
    return static_cast<std::int64_t>(std::llround(*real));
  }
  return std::nullopt;
}

}  // namespace

ImageInfo ImageInfo::of(syntax::Document& document, const syntax::Dictionary& dictionary,
                        const syntax::Object& colour_spaces) {
  ImageInfo info;
  const syntax::Object filter = entry(document, dictionary, "Filter");
  if (const std::string* name = filter.name()) {
    info.filters.emplace_back(syntax::Filters::full_name(*name));
  } else if (const syntax::Object::Array* list = filter.array()) {
    for (const syntax::Object& item : *list) {
      const syntax::Object resolved = document.resolve(item);
      if (const std::string* item_name = resolved.name()) {
        info.filters.emplace_back(syntax::Filters::full_name(*item_name));
      }
    }
  }
  if (!info.filters.empty()) {
    info.encoding = info.filters.back();
  }
  info.width = whole(entry(document, dictionary, "Width"));
  info.height = whole(entry(document, dictionary, "Height"));
  info.bits_per_component = whole(entry(document, dictionary, "BitsPerComponent"));
  info.image_mask = entry(document, dictionary, "ImageMask").boolean().value_or(false);
  info.interpolate = entry(document, dictionary, "Interpolate").boolean().value_or(false);
  const syntax::Object space = entry(document, dictionary, "ColorSpace");
  if (!space.is_null() && !info.image_mask) {
    try {
      const auto parsed = ColourSpace::parse(document, space, colour_spaces);
      info.colour_space = parsed->name();
      info.components = parsed->components();
    } catch (const DecodeFault&) {
      info.colour_space = "invalid";
    }
  }
  info.mask =
      std::string(Masks::name(Masks::kind(document, dictionary, info.encoding == "JPXDecode")));
  return info;
}

}  // namespace pdfkit::images
