// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// pdfkit_image_census: the census executable of the images layer's decoder
// gate. It opens one PDF with the library's own reader, lists every image the
// pages' resources reach (images::ImageCatalogue), decodes each one's base and
// mask, and prints one JSON object on standard output: per image its
// identity, object, pages, encoding, colour space, size, mask kind, whether it
// decoded (or the fault), the SHA-256 of its encoded bytes (to align it with
// PDFium's image object) and of its PNG, and the extracted bytes' SHA-256.
// With --dump <folder> it also writes each decoded base as "<identity>.pix":
// a text line "width height channels" then 8-bit samples, which
// tests/pdfium/image_agreement.py compares with PDFium's bitmap. It is a test
// tool built beside the unit tier, not part of the C ABI, because it measures
// the decoders before the content interpreter numbers occurrences.

#include "images/colour_space.hpp"
#include "images/decode_fault.hpp"
#include "images/image_catalogue.hpp"
#include "images/image_decoder.hpp"
#include "images/image_extractor.hpp"
#include "images/image_identity.hpp"
#include "images/masks.hpp"
#include "images/png_writer.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"
#include "platform/json_writer.hpp"
#include "syntax/document.hpp"
#include "syntax/limits.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using pdfkit::images::ImageCatalogue;
using pdfkit::images::ImageDecoder;
using pdfkit::platform::JsonWriter;
using pdfkit::platform::crypto::Sha2;

std::string hex(const std::vector<std::byte>& digest) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  for (const std::byte b : digest) {
    const auto v = std::to_integer<unsigned>(b);
    out.push_back(digits[v >> 4U]);
    out.push_back(digits[v & 15U]);
  }
  return out;
}

template <typename Bytes>
std::string sha256(const Bytes& bytes) {
  return hex(
      Sha2::digest(Sha2::Variant::sha256, std::as_bytes(std::span(bytes.data(), bytes.size()))));
}

std::string colour_space_name(pdfkit::syntax::Document& document,
                              const pdfkit::syntax::Dictionary& dictionary) {
  const pdfkit::syntax::Object stated = document.resolve_entry(dictionary, "ColorSpace");
  if (stated.is_null()) {
    return document.resolve_entry(dictionary, "ImageMask").boolean().value_or(false) ? "stencil"
                                                                                     : "none";
  }
  try {
    const auto space = pdfkit::images::ColourSpace::parse(document, stated);
    return space->name() + "/" + std::to_string(space->components());
  } catch (const pdfkit::images::DecodeFault&) {
    return "invalid";
  }
}

void dump(const std::string& folder, const std::string& identity,
          const pdfkit::images::PixelImage& image) {
  std::ofstream out(folder + "/" + identity.substr(4) + ".pix", std::ios::binary);
  out << image.width << ' ' << image.height << ' ' << image.channels << '\n';
  std::vector<char> row(static_cast<std::size_t>(image.width) * image.channels);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      for (std::uint32_t c = 0; c < image.channels; ++c) {
        row[static_cast<std::size_t>(x) * image.channels + c] =
            static_cast<char>(image.sample8(x, y, c));
      }
    }
    out.write(row.data(), static_cast<std::streamsize>(row.size()));
  }
}

// A decode whose budget refusal is the image's fault rather than the file's,
// so one oversized image does not hide the census of the others.
ImageDecoder::Result decode(pdfkit::syntax::Document& document,
                            const pdfkit::syntax::Stream& stream, ImageDecoder::Part part) {
  try {
    return ImageDecoder::decode(document, stream, part);
  } catch (const pdfkit::platform::Error& error) {
    ImageDecoder::Result result;
    result.fault =
        std::string(pdfkit::platform::error_code_name(error.code())) + ": " + error.failed();
    return result;
  }
}

void census(pdfkit::syntax::Document& document, JsonWriter& json, const std::string& folder) {
  const ImageCatalogue catalogue(document);
  json.key("images").begin_array();
  for (const ImageCatalogue::Entry& entry : catalogue.entries()) {
    const pdfkit::syntax::Stream& stream = *entry.stream.stream();
    const pdfkit::syntax::Dictionary& dictionary = stream.dictionary();
    json.begin_object();
    json.key("identity").string(entry.identity);
    json.key("object").unsigned_integer(entry.object_number);
    json.key("pages").begin_array();
    for (const std::uint32_t page : entry.pages) {
      json.unsigned_integer(page);
    }
    json.end_array();
    json.key("in_pattern").boolean(entry.in_pattern);
    json.key("encoding").string(ImageDecoder::encoding(document, dictionary));
    json.key("colour_space").string(colour_space_name(document, dictionary));
    json.key("width").integer(document.resolve_entry(dictionary, "Width").integer().value_or(0));
    json.key("height").integer(document.resolve_entry(dictionary, "Height").integer().value_or(0));
    json.key("bits").integer(
        document.resolve_entry(dictionary, "BitsPerComponent").integer().value_or(0));
    const bool jpx = ImageDecoder::encoding(document, dictionary) == "JPXDecode";
    json.key("mask").string(
        pdfkit::images::Masks::name(pdfkit::images::Masks::kind(document, dictionary, jpx)));
    json.key("raw_sha256")
        .string(sha256(pdfkit::images::ImageIdentity::encoded_bytes(document, stream)));
    const auto started = std::chrono::steady_clock::now();
    const ImageDecoder::Result base = decode(document, stream, ImageDecoder::Part::base);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    json.key("seconds").number(static_cast<double>(static_cast<long long>(seconds * 1000)) / 1000);
    json.key("decoded").boolean(base.image.has_value());
    json.key("damaged").boolean(base.damaged);
    json.key("fault").string(base.fault);
    if (base.image) {
      json.key("out_width").unsigned_integer(base.image->width);
      json.key("out_height").unsigned_integer(base.image->height);
      json.key("channels").unsigned_integer(base.image->channels);
      json.key("out_bits").unsigned_integer(base.image->bits);
      json.key("png_sha256").string(sha256(pdfkit::images::PngWriter::write(*base.image)));
      if (!folder.empty()) {
        dump(folder, entry.identity, *base.image);
      }
    }
    const ImageDecoder::Result mask = decode(document, stream, ImageDecoder::Part::mask);
    if (!mask.no_mask) {
      json.key("mask_decoded").boolean(mask.image.has_value());
      json.key("mask_fault").string(mask.fault);
      if (mask.image) {
        json.key("mask_png_sha256").string(sha256(pdfkit::images::PngWriter::write(*mask.image)));
      }
    }
    try {
      const pdfkit::images::ImageExtractor::Output extracted =
          pdfkit::images::ImageExtractor::extract(document, stream, {});
      json.key("extracted").string(extracted.format);
      json.key("extracted_sha256").string(extracted.fault.empty() ? sha256(extracted.bytes) : "");
    } catch (const pdfkit::platform::Error& error) {
      json.key("extracted").string(std::string(pdfkit::platform::error_code_name(error.code())));
    }
    json.end_object();
  }
  json.end_array();
}

// The open limits of the library, but a per-document budget large enough for
// every image to be decoded three times (base, mask, extraction): the census
// measures the decoders, and the per-stream limit still bounds each image.
pdfkit::syntax::Limits limits() {
  pdfkit::syntax::Limits value;
  value.document_bytes = 64ULL * 1024 * 1024 * 1024;
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: pdfkit_image_census <file.pdf> [--dump <folder>]\n";
    return 1;
  }
  std::string folder;
  if (argc >= 4 && std::string(argv[2]) == "--dump") {
    folder = argv[3];
  }
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) {
    std::cerr << "cannot read " << argv[1] << "\n";
    return 1;
  }
  const std::vector<char> text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    bytes[i] = static_cast<std::byte>(text[i]);
  }
  JsonWriter json;
  json.begin_object();
  try {
    const std::unique_ptr<pdfkit::syntax::Document> document =
        pdfkit::syntax::Document::open(bytes, limits());
    json.key("status").string("ok");
    census(*document, json, folder);
  } catch (const pdfkit::platform::Error& error) {
    JsonWriter refused;
    refused.begin_object();
    refused.key("status").string(pdfkit::platform::is_refusal(error.code()) ? "refused" : "error");
    refused.key("code").string(pdfkit::platform::error_code_name(error.code()));
    refused.key("message").string(error.failed());
    refused.end_object();
    std::cout << refused.take() << "\n";
    return 0;
  }
  json.end_object();
  std::cout << json.take() << "\n";
  return 0;
}
