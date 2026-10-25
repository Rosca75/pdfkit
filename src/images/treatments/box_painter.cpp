// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The nearest-colour search. The candidates are a grid over every
// component's sample range, endpoints included, as fine as the component
// count allows within about seventy thousand colour conversions: every value
// of a one-component image up to 12 bits (an Indexed image's whole palette),
// 33 levels per component of an RGB image, 13 of a CMYK one, and so on down
// to the two extremes of a many-colorant DeviceN image. Black and white lie on
// every grid, so the default fill and the caption colours are exact in every
// device space. The first candidate at the least distance wins, so the choice
// is a function of the image alone.

#include "images/treatments/box_painter.hpp"

#include "images/decode_fault.hpp"
#include "images/masks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdfkit::images::treatments {

namespace {

// The grid levels per component for a space of components components.
std::uint32_t levels(std::uint32_t components, std::uint32_t max_value) {
  std::uint32_t wanted = 2;
  if (components == 1) {
    wanted = 4097;
  } else if (components == 2) {
    wanted = 65;
  } else if (components == 3) {
    wanted = 33;
  } else if (components == 4) {
    wanted = 13;
  } else if (components <= 8) {
    wanted = 3;
  }
  return std::min(wanted, max_value + 1);
}

double distance(const BoxPainter::Colour& a, const BoxPainter::Colour& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < 3; ++i) {
    sum += (a[i] - b[i]) * (a[i] - b[i]);
  }
  return sum;
}

// Calls visit with every candidate tuple in a fixed order.
template <typename Visit>
void each_candidate(const BoxPainter::Meaning& meaning, Visit visit) {
  const auto max_value = static_cast<std::uint32_t>((1U << meaning.bits) - 1U);
  const std::uint32_t components = std::max<std::uint32_t>(meaning.components, 1);
  if (components > 16) {
    for (const std::uint16_t extreme : {std::uint16_t{0}, static_cast<std::uint16_t>(max_value)}) {
      const std::vector<std::uint16_t> tuple(components, extreme);
      visit(tuple);
    }
    return;
  }
  const std::uint32_t count = levels(components, max_value);
  std::vector<std::uint32_t> index(components, 0);
  std::vector<std::uint16_t> tuple(components, 0);
  while (true) {
    for (std::uint32_t c = 0; c < components; ++c) {
      tuple[c] = static_cast<std::uint16_t>(
          count == 1 ? 0 : std::lround(static_cast<double>(index[c]) * max_value / (count - 1)));
    }
    visit(tuple);
    std::uint32_t c = components;
    while (c > 0) {
      --c;
      if (++index[c] < count) {
        break;
      }
      index[c] = 0;
      if (c == 0) {
        return;
      }
    }
  }
}

}  // namespace

BoxPainter::Colour BoxPainter::colour_of(const Meaning& meaning,
                                         std::span<const std::uint16_t> samples) {
  const auto max_value = static_cast<double>((1U << meaning.bits) - 1U);
  const auto decoded = [&](std::size_t c) {
    const double raw = c < samples.size() ? samples[c] : 0.0;
    const double low = 2 * c < meaning.decode.size() ? meaning.decode[2 * c] : 0.0;
    const double high = (2 * c) + 1 < meaning.decode.size() ? meaning.decode[(2 * c) + 1] : 1.0;
    return low + (raw * (high - low) / max_value);
  };
  if (!meaning.space) {
    // A stencil: a 0 after /Decode paints (ISO 32000-1 section 8.9.6.2,
    // recalled).
    const double value = decoded(0) < 0.5 ? 0.0 : 1.0;
    return {value, value, value};
  }
  std::array<double, 32> in{};
  for (std::size_t c = 0; c < meaning.components && c < in.size(); ++c) {
    in[c] = decoded(c);
  }
  std::array<double, 3> out{};
  const std::size_t channels = meaning.space->output_channels();
  meaning.space->convert(
      std::span<const double>(in.data(), std::min<std::size_t>(meaning.components, in.size())),
      std::span<double>(out.data(), channels));
  if (channels == 1) {
    out[1] = out[0];
    out[2] = out[0];
  }
  return out;
}

std::vector<std::uint16_t> BoxPainter::nearest(const Meaning& meaning, const Colour& target) {
  std::vector<std::uint16_t> best;
  double best_distance = std::numeric_limits<double>::infinity();
  each_candidate(meaning, [&](const std::vector<std::uint16_t>& tuple) {
    if (!meaning.colour_key.empty() && Masks::keyed(tuple, meaning.colour_key)) {
      return;
    }
    Colour colour{};
    try {
      colour = colour_of(meaning, tuple);
    } catch (const DecodeFault&) {
      return;
    }
    const double d = distance(colour, target);
    if (d < best_distance) {
      best_distance = d;
      best = tuple;
    }
  });
  if (best.empty()) {
    best.assign(std::max<std::uint32_t>(meaning.components, 1), 0);
  }
  return best;
}

std::vector<std::uint16_t> BoxPainter::contrast(const Meaning& meaning,
                                                std::span<const std::uint16_t> fill) {
  Colour colour{0.0, 0.0, 0.0};
  try {
    colour = colour_of(meaning, fill);
  } catch (const DecodeFault&) {
    // Read as black: the caption is then white.
  }
  std::vector<std::uint16_t> light = nearest(meaning, {1.0, 1.0, 1.0});
  std::vector<std::uint16_t> dark = nearest(meaning, {0.0, 0.0, 0.0});
  const auto far = [&](const std::vector<std::uint16_t>& tuple) {
    try {
      return distance(colour_of(meaning, tuple), colour);
    } catch (const DecodeFault&) {
      return 0.0;
    }
  };
  return far(light) >= far(dark) ? light : dark;
}

void BoxPainter::paint(PixelBuffer& buffer, const PixelBuffer::Rect& rect,
                       std::span<const std::uint16_t> fill) {
  const PixelBuffer::Rect area = buffer.clip(rect);
  for (std::uint32_t y = area.top; y < area.bottom; ++y) {
    for (std::uint32_t x = area.left; x < area.right; ++x) {
      buffer.set_pixel(x, y, fill);
    }
  }
}

}  // namespace pdfkit::images::treatments
