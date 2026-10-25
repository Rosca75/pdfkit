// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// ImageIdentity computes an image's identity, CLAUDE.md section 4 and
// docs/DESIGN.md section 9.1: "img:" and the first 16 hexadecimal digits of
// the SHA-256 over the image's decisive dictionary entries (/Filter,
// /DecodeParms, /ColorSpace, /BitsPerComponent, /Decode, /SMask, /Mask,
// /Width, /Height, /ImageMask) serialised canonically, then its encoded bytes
// (decrypted, with every filter still applied). The canonical form sorts the
// keys, resolves every reference, writes integers and integral reals alike
// and other reals in their shortest form, expands the inline-image
// abbreviations of keys and names, and replaces a stream inside an entry by
// the SHA-256 of its encoded bytes and its own entries (a mask stream by its
// own identity), so the identity survives renumbering, encryption, page
// selection and a producer that stores one logo twice, and an inline image and
// an XObject of the same bytes and parameters share one.

#ifndef PDFKIT_IMAGES_IMAGE_IDENTITY_HPP
#define PDFKIT_IMAGES_IMAGE_IDENTITY_HPP

#include "platform/byte_span.hpp"
#include "syntax/dictionary.hpp"
#include "syntax/document.hpp"
#include "syntax/stream.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pdfkit::images {

class ImageIdentity {
 public:
  // The identity of an image XObject.
  [[nodiscard]] static std::string of_stream(syntax::Document& document,
                                             const syntax::Stream& stream);

  // The identity of an inline image: its dictionary as written (abbreviated
  // or not) and its data.
  [[nodiscard]] static std::string of_inline(syntax::Document& document,
                                             const syntax::Dictionary& dictionary,
                                             platform::ByteSpan data);

  // The encoded bytes of a stream: decrypted, no filter applied.
  [[nodiscard]] static std::vector<std::byte> encoded_bytes(syntax::Document& document,
                                                            const syntax::Stream& stream);

  // The full key of an inline image's abbreviated key (/W is /Width), or the
  // key itself.
  [[nodiscard]] static std::string full_key(const std::string& key);
};

}  // namespace pdfkit::images

#endif  // PDFKIT_IMAGES_IMAGE_IDENTITY_HPP
