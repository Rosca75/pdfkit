// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The placeholder's operators, one per line, every number through
// ContentSyntax so the bytes are a function of the values.

#include "redact/placeholder_drawing.hpp"

#include "redact/content_syntax.hpp"

namespace pdfkit::redact {

std::string PlaceholderDrawing::fill(const text::Colour& colour) {
  const std::vector<double>& c = colour.components;
  const auto component = [&c](std::size_t i) {
    return ContentSyntax::number(c[i]);
  };
  if (colour.family == "DeviceGray" && c.size() == 1) {
    return component(0) + " g\n";
  }
  if (colour.family == "DeviceRGB" && c.size() == 3) {
    return component(0) + " " + component(1) + " " + component(2) + " rg\n";
  }
  if (colour.family == "DeviceCMYK" && c.size() == 4) {
    return component(0) + " " + component(1) + " " + component(2) + " " + component(3) + " k\n";
  }
  if (c.empty() && (colour.family == "DeviceGray" || colour.family == "DeviceRGB" ||
                    colour.family == "DeviceCMYK")) {
    return "0 g\n";
  }
  return {};
}

std::string PlaceholderDrawing::operators(const text::GlyphRecord& anchor,
                                          std::string_view placeholder,
                                          writer::PlaceholderFont::Face face, double scale,
                                          bool invisible) {
  std::string out = "\nq\n";
  out += fill(anchor.fill);
  out += "BT\n/" + writer::PlaceholderFont::resource_name(face) + " " +
         ContentSyntax::number(anchor.font_size) + " Tf\n";
  out += ContentSyntax::matrix(anchor.text_matrix) + " Tm\n";
  out += ContentSyntax::number(anchor.horizontal_scaling * scale) + " Tz\n";
  out += ContentSyntax::number(anchor.rise) + " Ts\n0 Tc\n0 Tw\n";
  out += invisible ? "3 Tr\n" : "0 Tr\n";
  out += ContentSyntax::hex(writer::PlaceholderFont::codes(placeholder, face)) + " Tj\nET\nQ\n";
  return out;
}

}  // namespace pdfkit::redact
