// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// GraphicsState is the graphics and text state of ISO 32000-1 sections 8.4
// and 9.3 (read) that the interpreter needs, docs/DESIGN.md section 6.1: the
// CTM, the clip bound, the fill and stroke colours, the fill alpha, the
// soft-mask and blend-mode facts that decide whether a fill is opaque, and the
// text state parameters (Tc, Tw, Tz, TL, Tf font and size, Tr, Ts), with the
// q/Q stack. The text and line matrices are not part of the graphics state
// (section 9.4.2) and live in the interpreter. The stack is bounded at 256
// entries: a q past the bound is counted and saves nothing, and the Q that
// closes it restores nothing, so a hostile stream of nested q costs no memory
// and a well-formed stream inside the bound is read exactly. An unmatched Q
// is ignored; an unclosed q needs no closing, as nothing follows the end.

#ifndef PDFKIT_TEXT_GRAPHICS_STATE_HPP
#define PDFKIT_TEXT_GRAPHICS_STATE_HPP

#include "fonts/pdf_font.hpp"
#include "text/colour.hpp"
#include "text/matrix.hpp"
#include "text/rect.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pdfkit::text {

class GraphicsState {
 public:
  // The depth past which q saves nothing (an author decision, recorded in
  // docs/DESIGN.md section 6.1).
  static constexpr std::size_t max_depth = 256;

  struct State {
    Matrix ctm;
    // The clip bound in device space; everything until a W narrows it.
    Rect clip = Rect::everything();
    Colour fill{"DeviceGray", "DeviceGray", 1, {0.0}};
    Colour stroke{"DeviceGray", "DeviceGray", 1, {0.0}};
    // The /ca of the ExtGState in force.
    double fill_alpha = 1.0;
    // An ExtGState /SMask other than /None is in force.
    bool soft_mask = false;
    // The blend mode is /Normal (or /Compatible).
    bool normal_blend = true;

    // Text state: Tc, Tw, Tz (percent), TL, Ts, Tr, and the Tf font and size.
    double char_spacing = 0.0;
    double word_spacing = 0.0;
    double horizontal_scaling = 100.0;
    double leading = 0.0;
    double rise = 0.0;
    int render_mode = 0;
    double font_size = 0.0;
    std::shared_ptr<fonts::PdfFont> font;
    // The resource name the font was selected by, for messages.
    std::string font_resource;
  };

  [[nodiscard]] State& current() noexcept { return current_; }
  [[nodiscard]] const State& current() const noexcept { return current_; }

  // q: saves the state; false when the bound is reached and nothing was
  // saved.
  bool save();
  // Q: restores the last saved state; false for an unmatched Q.
  bool restore();
  [[nodiscard]] std::size_t depth() const noexcept { return stack_.size() + overflow_; }

 private:
  State current_;
  std::vector<State> stack_;
  std::size_t overflow_ = 0;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_GRAPHICS_STATE_HPP
