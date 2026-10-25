// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageOccurrence is one draw of an image, docs/DESIGN.md section 9.2 and
// CLAUDE.md section 4: a Do of an image XObject or one inline image (BI ...
// ID ... EI), numbered per page in draw order through forms from 0. It holds
// what the image layer needs to identify, decode and treat the picture
// without walking the content again: the image's object (or the inline
// image's dictionary and data), the CTM at the draw (the unit square under
// it is the image's place on the page), the device box, the form path, and
// where the drawing operator sits (stream walk and operator index), which a
// removal deletes. An occurrence is never named by a byte offset, because a
// redaction moves every offset; the stream walk and operator index are
// positions in the interpreter's own walk, which the writer repeats over the
// same objects.

#ifndef PDFKIT_TEXT_IMAGE_OCCURRENCE_HPP
#define PDFKIT_TEXT_IMAGE_OCCURRENCE_HPP

#include "syntax/object.hpp"
#include "text/matrix.hpp"
#include "text/rect.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pdfkit::text {

class ImageOccurrence {
 public:
  enum class Kind : std::uint8_t { xobject, inline_image };

  std::uint32_t page = 0;
  // The draw's place among the page's image draws, from 0, through forms.
  std::size_t ordinal = 0;
  // The page's event order shared with glyphs and paints.
  std::size_t sequence = 0;
  Kind kind = Kind::xobject;

  // For an XObject: the reference the resource names (number 0 for a direct
  // stream), the resource name used by Do, and the resolved stream object.
  syntax::Object::Reference reference;
  std::string resource_name;
  syntax::Object image;

  // For an inline image: its dictionary as written (abbreviated keys kept)
  // and its data, [data_start, data_end) of the stream's bytes, copied.
  syntax::Object dictionary;
  std::size_t data_start = 0;
  std::size_t data_end = 0;
  std::shared_ptr<const std::vector<std::byte>> data;

  // The content stream walk and the operator (Do or BI) that drew it.
  std::size_t stream = 0;
  std::size_t operation = 0;
  // The form XObjects the draw was reached through, outermost first, by
  // object number; empty for a draw in the page's own content.
  std::vector<std::uint32_t> form_path;

  // The CTM at the draw; the image occupies the unit square under it.
  Matrix ctm;
  // The unit square's bound under the CTM, and the clip bound in force.
  Rect box;
  Rect clip = Rect::everything();
  // The default optional-content configuration hides the draw.
  bool optional_content_off = false;
};

}  // namespace pdfkit::text

#endif  // PDFKIT_TEXT_IMAGE_OCCURRENCE_HPP
