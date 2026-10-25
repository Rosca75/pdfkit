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
// a text line "width height channels" then 8-bit samples (a stencil as 255
// where it paints and 0 elsewhere), which
// tests/pdfium/image_agreement.py compares with PDFium's bitmap; with
// --compare <list> it reads pairs of such a dump and a PDFium bitmap ("width
// height stride format" then the rows; format 1 grey, 2 BGR, 3 BGRx, 4 BGRA,
// as fpdfview.h numbers them) and prints per pair the mean absolute
// difference of each RGB channel, the largest difference and the pixels that
// differ, because comparing tens of megabytes of samples is a job for
// compiled code. It is a test
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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
        std::uint8_t value = image.sample8(x, y, c);
        if (image.transparent) {
          // A stencil is dumped as what it paints: 255 where it paints, as
          // PDFium's bitmap of an image mask holds it.
          value = value == *image.transparent ? 0 : 255;
        }
        row[static_cast<std::size_t>(x) * image.channels + c] = static_cast<char>(value);
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
  // Progress goes to standard error, so a file that runs out of time names
  // the step it was in.
  std::cerr << "walking the resources\n";
  const ImageCatalogue catalogue(document);
  std::cerr << "walked: " << catalogue.entries().size() << " images\n";
  json.key("images").begin_array();
  for (const ImageCatalogue::Entry& entry : catalogue.entries()) {
    const pdfkit::syntax::Stream& stream = *entry.stream.stream();
    std::cerr << entry.identity << " object " << entry.object_number << "\n";
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
    {
      // Whether the soft mask states /Matte, which the base's colours are
      // un-premultiplied by.
      const pdfkit::syntax::Object smask = document.resolve_entry(dictionary, "SMask");
      const pdfkit::syntax::Stream* soft = smask.stream();
      json.key("matte").boolean(soft != nullptr && soft->dictionary().contains("Matte"));
    }
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
      json.key("extracted").string(pdfkit::platform::error_code_name(error.code()));
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

struct Raster {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // PDFium's bitmap format; 0 for a census dump.
  std::uint64_t format = 0;
  // RGB, 8 bits per sample.
  std::vector<std::uint8_t> rgb;
};

// Reads a census dump or a PDFium bitmap into RGB; false when unreadable.
bool read_raster(const std::string& path, bool pdfium, Raster& raster) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::uint64_t width = 0;
  std::uint64_t height = 0;
  std::uint64_t third = 0;
  std::uint64_t format = 0;
  in >> width >> height >> third;
  if (pdfium) {
    in >> format;
  }
  in.get();
  if (!in || width == 0 || height == 0 || width > 100000 || height > 100000) {
    return false;
  }
  const std::uint64_t channels = pdfium ? (format == 1 ? 1 : format == 2 ? 3 : 4) : third;
  const std::uint64_t stride = pdfium ? third : width * channels;
  if (channels == 0 || channels > 4 || stride < width * channels) {
    return false;
  }
  std::vector<char> data(static_cast<std::size_t>(stride * height));
  in.read(data.data(), static_cast<std::streamsize>(data.size()));
  if (!in) {
    return false;
  }
  raster.format = format;
  raster.width = static_cast<std::uint32_t>(width);
  raster.height = static_cast<std::uint32_t>(height);
  raster.rgb.assign(static_cast<std::size_t>(width * height * 3), 0);
  for (std::uint64_t y = 0; y < height; ++y) {
    for (std::uint64_t x = 0; x < width; ++x) {
      const auto from = static_cast<std::size_t>(y * stride + x * channels);
      const auto at = [&](std::size_t i) {
        return static_cast<std::uint8_t>(data[from + i]);
      };
      std::uint8_t* to = raster.rgb.data() + static_cast<std::size_t>((y * width + x) * 3);
      if (channels == 1) {
        to[0] = to[1] = to[2] = at(0);
      } else if (pdfium) {
        to[0] = at(2);
        to[1] = at(1);
        to[2] = at(0);
      } else {
        to[0] = at(0);
        to[1] = at(1);
        to[2] = at(2);
      }
    }
  }
  return true;
}

int compare(const std::string& list) {
  std::ifstream in(list);
  std::string mine;
  std::string theirs;
  while (in >> mine >> theirs) {
    JsonWriter json;
    json.begin_object();
    json.key("mine").string(mine);
    Raster a;
    Raster b;
    if (!read_raster(mine, false, a) || !read_raster(theirs, true, b)) {
      json.key("status").string("unreadable");
    } else if (a.width != b.width || a.height != b.height) {
      json.key("status").string("size");
      json.key("pdfium_width").unsigned_integer(b.width);
      json.key("pdfium_height").unsigned_integer(b.height);
    } else {
      std::array<std::uint64_t, 3> sums{};
      std::uint64_t differing = 0;
      int largest = 0;
      const std::size_t pixels = static_cast<std::size_t>(a.width) * a.height;
      for (std::size_t i = 0; i < pixels; ++i) {
        bool differs = false;
        for (std::size_t c = 0; c < 3; ++c) {
          const int d = std::abs(int{a.rgb[i * 3 + c]} - int{b.rgb[i * 3 + c]});
          sums[c] += static_cast<std::uint64_t>(d);
          largest = std::max(largest, d);
          differs = differs || d != 0;
        }
        differing += differs ? 1U : 0U;
      }
      double mad = 0.0;
      for (const std::uint64_t sum : sums) {
        mad = std::max(mad, static_cast<double>(sum) / static_cast<double>(pixels));
      }
      json.key("status").string("compared");
      json.key("pdfium_format").unsigned_integer(b.format);
      json.key("mad").fixed(mad, 3);
      json.key("largest").integer(largest);
      json.key("differing").unsigned_integer(differing);
      json.key("pixels").unsigned_integer(pixels);
    }
    json.end_object();
    std::cout << json.take() << "\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: pdfkit_image_census <file.pdf> [--dump <folder>] | --compare <list>\n";
    return 1;
  }
  if (argc == 3 && std::string(argv[1]) == "--compare") {
    return compare(argv[2]);
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
