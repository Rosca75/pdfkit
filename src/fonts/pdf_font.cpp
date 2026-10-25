// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The shared half of every font: splitting an operand into codes through the
// subclass, the per-code cache of the ladder's answers (the ladder is a pure
// function of the code once the font is loaded), the unreadable count and the
// fault notes.

#include "fonts/pdf_font.hpp"

#include <algorithm>

namespace pdfkit::fonts {

std::vector<PdfFont::DecodedCode> PdfFont::decode(platform::ByteSpan bytes) {
  std::vector<DecodedCode> out;
  std::size_t at = 0;
  while (at < bytes.size()) {
    const auto [code, length] = next_code(bytes, at);
    const std::size_t taken = std::clamp<std::size_t>(length, 1, bytes.size() - at);
    const Answer& decoded = answer(code);
    if (decoded.rung == DecodingRung::unreadable) {
      ++unreadable_count_;
    }
    out.push_back({code, taken, decoded.text, decoded.rung, decoded.decomposed});
    at += taken;
  }
  return out;
}

const PdfFont::Answer& PdfFont::answer(std::uint32_t code) {
  const auto found = answers_.find(code);
  if (found != answers_.end()) {
    return found->second;
  }
  return answers_.emplace(code, decode_code(code)).first->second;
}

double PdfFont::width(std::uint32_t code) const {
  return code_width(code);
}

PdfFont::VerticalAdvance PdfFont::vertical_advance(std::uint32_t code) const {
  VerticalAdvance advance;
  advance.vx = code_width(code) / 2.0;
  return advance;
}

void PdfFont::fault(std::string message) {
  if (std::find(faults_.begin(), faults_.end(), message) == faults_.end()) {
    faults_.push_back(std::move(message));
  }
}

std::string_view PdfFont::kind_name(Kind kind) noexcept {
  switch (kind) {
    case Kind::type1:
      return "Type1";
    case Kind::mm_type1:
      return "MMType1";
    case Kind::true_type:
      return "TrueType";
    case Kind::type3:
      return "Type3";
    case Kind::type0:
      return "Type0";
  }
  return "Type1";
}

std::string_view PdfFont::program_name(Program program) noexcept {
  switch (program) {
    case Program::none:
      return "none";
    case Program::type1:
      return "Type1";
    case Program::cff:
      return "CFF";
    case Program::true_type:
      return "TrueType";
    case Program::open_type:
      return "OpenType";
  }
  return "none";
}

}  // namespace pdfkit::fonts
