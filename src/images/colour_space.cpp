// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The colour spaces an image can be stated in and their conversion to grey or
// RGB. The Lab conversion follows ISO 32000-1 section 8.6.5.4 (recalled) to
// CIE XYZ under the space's white point, adapts XYZ to D65 by scaling each
// coordinate by the ratio of the white points, and encodes sRGB with the IEC
// 61966-2-1 matrix and transfer curve (recalled).

#include "images/colour_space.hpp"

#include "images/decode_fault.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/stream.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace pdfkit::images {

namespace {

using syntax::Dictionary;
using syntax::Document;
using syntax::Object;

constexpr int max_depth = 8;

[[noreturn]] void fault(const std::string& what) {
  throw DecodeFault("the colour space " + what);
}

double srgb_encode(double linear) {
  linear = std::clamp(linear, 0.0, 1.0);
  return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

double lab_inverse(double t) {
  return t >= 6.0 / 29.0 ? t * t * t : (108.0 / 841.0) * (t - 4.0 / 29.0);
}

std::vector<double> number_array(Document& document, const Object& value) {
  std::vector<double> out;
  if (const Object::Array* list = document.resolve(value).array()) {
    for (const Object& item : *list) {
      out.push_back(document.resolve(item).number().value_or(0.0));
    }
  }
  return out;
}

}  // namespace

std::shared_ptr<const ColourSpace> ColourSpace::device(std::size_t components) {
  std::shared_ptr<ColourSpace> space(new ColourSpace());
  space->components_ = components;
  if (components == 3) {
    space->family_ = Family::device_rgb;
    space->output_channels_ = 3;
    space->name_ = "DeviceRGB";
  } else if (components == 4) {
    space->family_ = Family::device_cmyk;
    space->output_channels_ = 3;
    space->name_ = "DeviceCMYK";
  } else if (components == 1) {
    space->family_ = Family::device_gray;
    space->output_channels_ = 1;
    space->name_ = "DeviceGray";
  } else {
    fault("of " + std::to_string(components) + " components has no device equivalent");
  }
  return space;
}

std::shared_ptr<const ColourSpace> ColourSpace::parse(Document& document, const Object& object,
                                                      const Object& colour_spaces, int depth) {
  if (depth > max_depth) {
    fault("nests other spaces more than " + std::to_string(max_depth) + " deep");
  }
  const Object value = document.resolve(object);
  if (const std::string* name = value.name()) {
    if (*name == "DeviceGray" || *name == "G" || *name == "CalGray") {
      return device(1);
    }
    if (*name == "DeviceRGB" || *name == "RGB" || *name == "CalRGB") {
      return device(3);
    }
    if (*name == "DeviceCMYK" || *name == "CMYK") {
      return device(4);
    }
    if (*name == "Pattern") {
      fault("Pattern cannot colour an image");
    }
    if (const Dictionary* named = document.resolve(colour_spaces).dictionary()) {
      if (const Object* entry = named->find(*name)) {
        return parse(document, *entry, Object(), depth + 1);
      }
    }
    fault("/" + *name + " is not a space an image can use and no resource names it");
  }
  const Object::Array* array = value.array();
  if (array == nullptr || array->empty()) {
    fault("is neither a name nor an array");
  }
  const Object family_object = document.resolve((*array)[0]);
  const std::string* family = family_object.name();
  if (family == nullptr) {
    fault("array does not start with a name");
  }
  const auto item = [&](std::size_t index) {
    return index < array->size() ? document.resolve((*array)[index]) : Object();
  };
  if (*family == "DeviceGray" || *family == "G" || *family == "CalGray") {
    return device(1);
  }
  if (*family == "DeviceRGB" || *family == "RGB" || *family == "CalRGB") {
    return device(3);
  }
  if (*family == "DeviceCMYK" || *family == "CMYK") {
    return device(4);
  }
  if (*family == "ICCBased") {
    const Object stream = item(1);
    const Dictionary* dictionary = stream.dictionary_or_stream_dictionary();
    if (dictionary == nullptr) {
      fault("ICCBased has no profile stream");
    }
    std::int64_t n = document.resolve_entry(*dictionary, "N").integer().value_or(0);
    const Object alternate = document.resolve_entry(*dictionary, "Alternate");
    if (n != 1 && n != 3 && n != 4) {
      // A missing or wrong /N is read from the alternate space, as viewers do.
      if (alternate.is_null()) {
        fault("ICCBased has the component count " + std::to_string(n));
      }
      n = static_cast<std::int64_t>(
          parse(document, alternate, colour_spaces, depth + 1)->components());
    }
    if (!alternate.is_null() && depth < max_depth) {
      // A Lab alternate is honoured; every other alternate is the device
      // space of the same component count, which needs no conversion here.
      const Object alternate_value = document.resolve(alternate);
      const Object::Array* list = alternate_value.array();
      if (list != nullptr && !list->empty() && document.resolve((*list)[0]).is_name("Lab")) {
        auto lab = parse(document, alternate, colour_spaces, depth + 1);
        if (lab->components() == static_cast<std::size_t>(n)) {
          return lab;
        }
      }
    }
    std::shared_ptr<ColourSpace> space(new ColourSpace(*device(static_cast<std::size_t>(n))));
    space->name_ = "ICCBased";
    space->range_ = number_array(document, dictionary->get("Range"));
    if (space->range_.size() != 2 * space->components_) {
      space->range_.clear();
    }
    return space;
  }
  if (*family == "Lab") {
    std::shared_ptr<ColourSpace> space(new ColourSpace());
    space->family_ = Family::lab;
    space->components_ = 3;
    space->output_channels_ = 3;
    space->name_ = "Lab";
    const Object dictionary_object = item(1);
    if (const Dictionary* dictionary = dictionary_object.dictionary()) {
      const std::vector<double> white = number_array(document, dictionary->get("WhitePoint"));
      if (white.size() == 3 && white[0] > 0 && white[1] > 0 && white[2] > 0) {
        space->white_ = {white[0], white[1], white[2]};
      }
      const std::vector<double> range = number_array(document, dictionary->get("Range"));
      if (range.size() == 4) {
        space->range_ = {0.0, 100.0, range[0], range[1], range[2], range[3]};
      }
    }
    if (space->range_.empty()) {
      space->range_ = {0.0, 100.0, -100.0, 100.0, -100.0, 100.0};
    }
    return space;
  }
  if (*family == "Indexed" || *family == "I") {
    std::shared_ptr<ColourSpace> space(new ColourSpace());
    space->family_ = Family::indexed;
    space->name_ = "Indexed";
    space->base_ = parse(document, item(1), colour_spaces, depth + 1);
    if (space->base_->family() == Family::indexed) {
      fault("Indexed has an Indexed base");
    }
    space->components_ = 1;
    space->output_channels_ = space->base_->output_channels();
    const std::int64_t hival =
        item(2).integer().value_or(static_cast<std::int64_t>(item(2).number().value_or(0.0)));
    space->hival_ = static_cast<std::uint32_t>(std::clamp<std::int64_t>(hival, 0, 255));
    const Object lookup = item(3);
    if (const Object::String* text = lookup.string()) {
      for (const char c : text->bytes) {
        space->lookup_.push_back(static_cast<std::uint8_t>(c));
      }
    } else if (const syntax::Stream* stream = lookup.stream()) {
      for (const std::byte b : document.decode(*stream).bytes) {
        space->lookup_.push_back(std::to_integer<std::uint8_t>(b));
      }
    } else {
      fault("Indexed has no lookup string or stream");
    }
    const std::size_t entry = space->base_->components();
    const std::size_t needed = (static_cast<std::size_t>(space->hival_) + 1) * entry;
    // A short lookup is padded with zeros, as viewers read it.
    space->lookup_.resize(std::max(needed, space->lookup_.size()), 0);
    return space;
  }
  if (*family == "Separation" || *family == "DeviceN") {
    std::shared_ptr<ColourSpace> space(new ColourSpace());
    const bool separation = *family == "Separation";
    space->family_ = separation ? Family::separation : Family::device_n;
    space->name_ = *family;
    const Object names = item(1);
    if (separation) {
      space->components_ = 1;
      space->none_ = names.is_name("None");
    } else {
      const Object::Array* list = names.array();
      if (list == nullptr || list->empty() || list->size() > 32) {
        fault("DeviceN has no usable list of colorant names");
      }
      space->components_ = list->size();
      space->none_ = std::all_of(list->begin(), list->end(), [&](const Object& colorant) {
        return document.resolve(colorant).is_name("None");
      });
    }
    space->base_ = parse(document, item(2), colour_spaces, depth + 1);
    if (space->base_->family() == Family::indexed) {
      fault(*family + " has an Indexed alternate space");
    }
    space->output_channels_ = space->base_->output_channels();
    space->tint_ = PdfFunction::parse(document, item(3), 0);
    if (space->tint_->inputs() != space->components_ && !space->none_) {
      fault(*family + "'s tint transform takes " + std::to_string(space->tint_->inputs()) +
            " inputs for " + std::to_string(space->components_) + " components");
    }
    return space;
  }
  if (*family == "Pattern") {
    fault("Pattern cannot colour an image");
  }
  fault("family /" + *family + " is not one ISO 32000-1 defines");
}

std::vector<double> ColourSpace::default_decode(std::uint32_t bits) const {
  std::vector<double> decode;
  if (family_ == Family::indexed) {
    decode = {0.0, std::ldexp(1.0, static_cast<int>(bits)) - 1.0};
    return decode;
  }
  if (!range_.empty()) {
    return range_;
  }
  for (std::size_t i = 0; i < components_; ++i) {
    decode.push_back(0.0);
    decode.push_back(1.0);
  }
  return decode;
}

void ColourSpace::convert(std::span<const double> in, std::span<double> out) const {
  const auto c = [&](std::size_t i) {
    return i < in.size() ? in[i] : 0.0;
  };
  switch (family_) {
    case Family::device_gray:
      out[0] = std::clamp(c(0), 0.0, 1.0);
      return;
    case Family::device_rgb:
      for (std::size_t i = 0; i < 3; ++i) {
        out[i] = std::clamp(c(i), 0.0, 1.0);
      }
      return;
    case Family::device_cmyk: {
      const double k = std::clamp(c(3), 0.0, 1.0);
      for (std::size_t i = 0; i < 3; ++i) {
        out[i] = (1.0 - std::clamp(c(i), 0.0, 1.0)) * (1.0 - k);
      }
      return;
    }
    case Family::lab: {
      const double l = c(0);
      const double a = std::clamp(c(1), range_[2], range_[3]);
      const double b = std::clamp(c(2), range_[4], range_[5]);
      const double m = (l + 16.0) / 116.0;
      const double x = white_[0] * lab_inverse(m + a / 500.0) * (0.9505 / white_[0]);
      const double y = white_[1] * lab_inverse(m) * (1.0 / white_[1]);
      const double z = white_[2] * lab_inverse(m - b / 200.0) * (1.089 / white_[2]);
      out[0] = srgb_encode(3.2406 * x - 1.5372 * y - 0.4986 * z);
      out[1] = srgb_encode(-0.9689 * x + 1.8758 * y + 0.0415 * z);
      out[2] = srgb_encode(0.0557 * x - 0.2040 * y + 1.0570 * z);
      return;
    }
    case Family::indexed: {
      const double index = std::clamp(std::round(c(0)), 0.0, static_cast<double>(hival_));
      const std::size_t entry = base_->components();
      const std::size_t offset = static_cast<std::size_t>(index) * entry;
      const std::vector<double> decode = base_->default_decode(8);
      std::array<double, 4> values{};
      for (std::size_t i = 0; i < entry && i < values.size(); ++i) {
        const double byte = offset + i < lookup_.size() ? lookup_[offset + i] : 0.0;
        values[i] = decode[2 * i] + byte / 255.0 * (decode[2 * i + 1] - decode[2 * i]);
      }
      base_->convert(std::span<const double>(values.data(), std::min<std::size_t>(entry, 4)), out);
      return;
    }
    case Family::separation:
    case Family::device_n: {
      if (none_) {
        std::fill(out.begin(), out.end(), 1.0);
        return;
      }
      std::array<double, 32> alternate{};
      const std::size_t count = std::min(tint_->outputs(), alternate.size());
      std::array<double, 32> inputs{};
      for (std::size_t i = 0; i < components_ && i < inputs.size(); ++i) {
        inputs[i] = std::clamp(c(i), 0.0, 1.0);
      }
      tint_->evaluate(std::span<const double>(inputs.data(), std::min(components_, inputs.size())),
                      std::span<double>(alternate.data(), count));
      base_->convert(std::span<const double>(alternate.data(), count), out);
      return;
    }
  }
}

}  // namespace pdfkit::images
