// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The classification pass. The two classes that compare glyphs with what was
// painted around them (white_fill and covered) look only at the candidates
// that can decide them: white_fill only for white glyphs, covered only against
// opaque rectangles and unmasked images; past a bound on the work (a page of
// very many glyphs and very many such shapes) covered is not decided and the
// page notes it, because a reader that takes minutes on one page has failed
// its caller.

#include "text/hidden_text.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

namespace pdfkit::text {

namespace {

constexpr double tolerance = 0.01;
// The glyph and shape comparisons one page may cost for the covered class.
constexpr std::uint64_t max_comparisons = 50'000'000;
// How many earlier glyphs of one code and font a glyph is compared with for
// the duplicate fold.
constexpr std::size_t duplicate_window = 8;

[[nodiscard]] Rect rect_of(const syntax::Page::Box& box) {
  return Rect::of(box.left, box.bottom, box.right, box.top);
}

[[nodiscard]] bool paints_white(const GlyphRecord& glyph) {
  switch (glyph.render_mode) {
    case 0:
    case 4:
      return glyph.fill.is_white();
    case 1:
    case 5:
      return glyph.stroke_white;
    case 2:
    case 6:
      return glyph.fill.is_white() && glyph.stroke_white;
    default:
      return false;
  }
}

[[nodiscard]] bool image_opaque(const ImageOccurrence& image) {
  const syntax::Object& value =
      image.kind == ImageOccurrence::Kind::xobject ? image.image : image.dictionary;
  const syntax::Dictionary* entries = value.dictionary_or_stream_dictionary();
  if (entries == nullptr) {
    return false;
  }
  const auto mask = [&](std::string_view key) {
    const syntax::Object* found = entries->find(key);
    return found != nullptr && found->boolean().value_or(false);
  };
  return !entries->contains("SMask") && !entries->contains("Mask") && !mask("ImageMask") &&
         !mask("IM") && !entries->contains("SMaskInData");
}

// A shape that can decide white_fill or covered: its box, when it was
// drawn, whether it is a non-white fill or an image, whether it covers.
struct Shape {
  Rect box;
  std::size_t sequence = 0;
  bool under_counts = false;
  bool covers = false;
};

void fold_duplicates(std::vector<GlyphRecord>& glyphs) {
  // A font is compared by its name (subset prefix included), so the same
  // face stated as two font objects (a stamp that adds its own Helvetica on
  // every draw) is one font.
  std::map<std::pair<std::string, std::uint32_t>, std::vector<std::size_t>> seen;
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    GlyphRecord& glyph = glyphs[i];
    // A glyph of no size draws at one point with every other glyph of its
    // string, so nearness says nothing about it.
    if (glyph.box.is_empty() || glyph.size < 0.01) {
      continue;
    }
    std::vector<std::size_t>& earlier =
        seen[{glyph.font != nullptr ? glyph.font->name() : std::string(), glyph.code}];
    bool folded = false;
    const std::size_t from =
        earlier.size() > duplicate_window ? earlier.size() - duplicate_window : 0;
    for (std::size_t k = earlier.size(); k > from; --k) {
      GlyphRecord& kept = glyphs[earlier[k - 1]];
      const double limit = 0.1 * std::max(glyph.size, kept.size);
      if (std::abs(glyph.origin.x - kept.origin.x) <= limit &&
          std::abs(glyph.origin.y - kept.origin.y) <= limit) {
        glyph.duplicate_of = kept.draw;
        glyph.hidden |= hidden_bit(HiddenClass::duplicate);
        kept.duplicates.push_back(glyph.draw);
        folded = true;
        break;
      }
    }
    if (!folded) {
      earlier.push_back(i);
    }
  }
}

}  // namespace

void HiddenText::classify(PageContent& content) {
  const Rect media = rect_of(content.media_box);
  const Rect crop = rect_of(content.crop_box);
  std::vector<Shape> shapes;
  for (const FigureRecord& figure : content.figures) {
    for (const FigureRecord::Paint& paint : figure.paints) {
      if (!paint.filled || paint.box.is_empty()) {
        continue;
      }
      shapes.push_back(
          {paint.box, paint.sequence, !paint.white, paint.opaque && paint.rectangular});
    }
  }
  for (const ImageOccurrence& image : content.images) {
    const Rect box = image.box.intersection(image.clip);
    if (box.is_empty()) {
      continue;
    }
    shapes.push_back({box, image.sequence, true, image_opaque(image) && image.ctm.axis_aligned()});
  }
  std::ranges::stable_sort(shapes, {}, &Shape::sequence);
  std::vector<Shape> covering;
  for (const Shape& shape : shapes) {
    if (shape.covers) {
      covering.push_back(shape);
    }
  }
  const bool decide_covered =
      static_cast<std::uint64_t>(content.glyphs.size()) * covering.size() <= max_comparisons;
  if (!decide_covered) {
    content.fault(
        "the page draws too many glyphs and filled shapes to decide which glyphs are "
        "covered; none is flagged covered");
  }
  for (GlyphRecord& glyph : content.glyphs) {
    std::uint16_t mask = 0;
    if (glyph.render_mode == 3 || glyph.render_mode == 7) {
      mask |= hidden_bit(HiddenClass::render_invisible);
    }
    if (glyph.optional_content_off) {
      mask |= hidden_bit(HiddenClass::optional_content_off);
    }
    const Rect& box = glyph.box;
    const bool placed = !box.is_empty();
    if (placed && glyph.clip.excludes(box, tolerance)) {
      mask |= hidden_bit(HiddenClass::clipped);
    }
    const bool off_media = placed && media.excludes(box, tolerance);
    if (off_media) {
      mask |= hidden_bit(HiddenClass::off_media);
    } else if (placed && crop.excludes(box, tolerance)) {
      mask |= hidden_bit(HiddenClass::off_crop);
    }
    if (paints_white(glyph)) {
      const bool on_something = std::ranges::any_of(shapes, [&](const Shape& shape) {
        return shape.sequence < glyph.sequence && shape.under_counts && shape.box.intersects(box);
      });
      if (!on_something) {
        mask |= hidden_bit(HiddenClass::white_fill);
      }
    }
    if (placed && decide_covered) {
      const auto later = std::ranges::upper_bound(covering, glyph.sequence, {}, &Shape::sequence);
      for (auto it = later; it != covering.end(); ++it) {
        if (it->box.contains(box, tolerance)) {
          mask |= hidden_bit(HiddenClass::covered);
          break;
        }
      }
    }
    glyph.hidden = static_cast<std::uint16_t>(glyph.hidden | mask);
  }
  fold_duplicates(content.glyphs);
}

}  // namespace pdfkit::text
