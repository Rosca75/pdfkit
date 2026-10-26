// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The stage in its order: find the objects of each treated identity, treat
// and replace them with their masks, walk the content for the removals and
// the inline images, empty the removed objects, report. The objects of an
// identity are found by hashing every image stream of the document as the
// graph holds it, because one picture stored twice is one identity with two
// objects and both must change. A treated inline image is written with the
// one filter /AHx over its samples as they are (an inline image is small,
// and a filter array is not read by every reader: PDFium decodes no bitmap
// from an inline [/AHx /Fl] image, measured on the dataset), so its data is
// hexadecimal and can never hold the bytes "EI" that end it (ISO 32000-1
// section 8.9.7, recalled). An inline image that does not decode at write time loses its
// draw rather than keep its original data.

#include "redact/image_treatment_applier.hpp"

#include "images/image_identity.hpp"
#include "images/treatments/box_painter.hpp"
#include "images/treatments/caption_drawer.hpp"
#include "images/treatments/mosaic_blur.hpp"
#include "images/treatments/reencoder.hpp"
#include "images/treatments/soft_mask_treatment.hpp"
#include "platform/crypto/sha2.hpp"
#include "platform/error.hpp"
#include "redact/image_remover.hpp"
#include "writer/serialiser.hpp"
#include "writer/stream_copier.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pdfkit::redact {

namespace {

using images::ImageDecoder;
using images::treatments::BoxPainter;
using images::treatments::CaptionDrawer;
using images::treatments::MosaicBlur;
using images::treatments::PixelBuffer;
using images::treatments::Reencoder;
using images::treatments::SoftMaskTreatment;
using syntax::Dictionary;
using syntax::Object;
using Treatment = ImageTreatmentPlan::Treatment;

BoxPainter::Meaning meaning_of(const ImageDecoder::Samples& samples) {
  BoxPainter::Meaning meaning;
  meaning.space = samples.space;
  meaning.decode = samples.decode;
  meaning.bits = samples.buffer.bits();
  meaning.components = samples.buffer.components();
  meaning.colour_key = samples.colour_key;
  return meaning;
}

std::string hex(platform::ByteSpan data) {
  const auto digest = platform::crypto::Sha2::digest(platform::crypto::Sha2::Variant::sha256, data);
  constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (const std::byte value : digest) {
    const auto byte = std::to_integer<unsigned>(value);
    out += digits[byte >> 4U];
    out += digits[byte & 0x0FU];
  }
  return out;
}

// A mask's samples treated over the image's rectangles.
void treat_mask(PixelBuffer& mask, const BoxPainter::Meaning& meaning, const Treatment& treatment,
                std::uint32_t width, std::uint32_t height) {
  for (const PixelBuffer::Rect& rect : treatment.blurs) {
    SoftMaskTreatment::blur(mask, rect, width, height, treatment.block, meaning);
  }
  for (const ImageTreatmentPlan::Box& box : treatment.boxes) {
    SoftMaskTreatment::opaque(mask, box.pixels, width, height, meaning);
  }
}

// The created stream of a re-encoded image.
syntax::Stream stream_of(Reencoder::Result result) {
  if (result.jpeg) {
    return {std::move(result.dictionary), std::move(result.data)};
  }
  return writer::StreamCopier::compress(std::move(result.dictionary), result.data);
}

class Stage {
 public:
  Stage(EditPlan& plan, writer::ObjectGraph& graph, writer::WriteReport& report)
      : plan_(plan), graph_(graph), report_(report), document_(plan.document()) {}

  void run() {
    const auto& treatments = plan_.images().treatments();
    // The image objects of every treated identity.
    std::map<std::string, std::vector<std::uint32_t>> objects;
    for (const auto& [number, unused] : document_.entries()) {
      const Object value = graph_.get(number);
      const syntax::Stream* stream = value.stream();
      if (stream == nullptr) {
        continue;
      }
      const Object* subtype = stream->dictionary().find("Subtype");
      if (subtype == nullptr || subtype->name() == nullptr || *subtype->name() != "Image") {
        continue;
      }
      std::string identity = images::ImageIdentity::of_stream(document_, *stream);
      if (treatments.contains(identity)) {
        objects[std::move(identity)].push_back(number);
      }
    }
    ImageRemover::Request request;
    for (const auto& [identity, treatment] : treatments) {
      writer::WriteReport::Image& entry = images_[identity];
      entry.identity = identity;
      entry.treatment = treatment.kind ? std::string(ImageTreatmentPlan::name(*treatment.kind))
                                       : std::string("region");
      const auto found = objects.find(identity);
      const std::vector<std::uint32_t> none;
      const std::vector<std::uint32_t>& numbers = found != objects.end() ? found->second : none;
      entry.objects = numbers.size();
      if (treatment.kind == ImageTreatmentPlan::Kind::remove) {
        for (const std::uint32_t number : numbers) {
          request.objects.emplace(number, identity);
        }
        request.inline_identities.insert(identity);
        continue;
      }
      for (const std::uint32_t number : numbers) {
        entry.sha256.push_back(treat_object(number, treatment));
      }
      request.inline_treated.insert(identity);
    }
    request.edit = [this, &treatments](const std::string& identity, const Dictionary& written,
                                       platform::ByteSpan data,
                                       const Object& colour_spaces) -> std::optional<std::string> {
      const auto known = inline_cache_.find(identity);
      if (known != inline_cache_.end()) {
        return known->second;
      }
      std::string digest;
      std::string replaced =
          treat_inline(written, data, colour_spaces, treatments.at(identity), digest);
      if (!digest.empty()) {
        images_[identity].sha256.push_back(std::move(digest));
      }
      inline_cache_.emplace(identity, replaced);
      return replaced;
    };
    const std::vector<std::uint32_t> pages = page_objects();
    const ImageRemover::Result walked = ImageRemover::apply(graph_, pages, request);
    for (const auto& [identity, count] : walked.removed) {
      images_[identity].draws_removed += count;
      if (request.inline_identities.contains(identity) &&
          !std::ranges::any_of(request.objects,
                               [&](const auto& object) { return object.second == identity; })) {
        images_[identity].inline_draws += count;
      }
    }
    for (const auto& [identity, count] : walked.replaced) {
      images_[identity].inline_draws += count;
    }
    // Nothing reaches a removed object any more; emptying it makes sure its
    // bytes cannot be written even through a reference the walk did not see.
    for (const auto& [number, identity] : request.objects) {
      graph_.set(number, Object());
    }
    for (auto& [identity, entry] : images_) {
      report_.images.push_back(std::move(entry));
    }
    rectangles(treatments);
  }

 private:
  // The output's page objects, in order.
  std::vector<std::uint32_t> page_objects() {
    std::vector<std::uint32_t> out;
    const Object catalog = graph_.get(graph_.root());
    const Dictionary* entries = catalog.dictionary();
    const Object tree = entries != nullptr ? graph_.resolve_entry(*entries, "Pages") : Object();
    const Dictionary* node = tree.dictionary();
    const Object kids = node != nullptr ? graph_.resolve_entry(*node, "Kids") : Object();
    if (const Object::Array* list = kids.array()) {
      for (const Object& kid : *list) {
        if (const auto reference = kid.reference()) {
          out.push_back(reference->number);
        }
      }
    }
    return out;
  }

  ImageDecoder::Samples read(const syntax::Stream& stream, bool soft_mask,
                             const std::string& what) {
    ImageDecoder::SamplesResult result = ImageDecoder::samples(document_, stream, soft_mask);
    if (!result.samples) {
      throw platform::Error(
          platform::ErrorCode::internal,
          "pdfkit_write could not decode " + what + " again to treat it: " + result.fault,
          "the image to decode as it did when the treatment was added",
          "this is a defect in pdfkit; report it with the input file");
    }
    return std::move(*result.samples);
  }

  // Treats the image object number; returns the SHA-256 of its new bytes.
  std::string treat_object(std::uint32_t number, const Treatment& treatment) {
    const Object value = graph_.get(number);
    const syntax::Stream& stream = *value.stream();
    const std::string what = "the image in object " + std::to_string(number);
    ImageDecoder::Samples samples = read(stream, false, what);
    ImageTreatmentApplier::treat(samples, treatment);
    Dictionary dictionary = stream.dictionary();
    const std::uint32_t width = samples.buffer.width();
    const std::uint32_t height = samples.buffer.height();
    const Object& smask = dictionary.get("SMask");
    const Object& mask = dictionary.get("Mask");
    if (const auto reference = smask.reference()) {
      if (!treat_mask_object(reference->number, true, treatment, width, height)) {
        dictionary.erase("SMask");
      }
    } else if (const auto stencil = mask.reference()) {
      if (!treat_mask_object(stencil->number, false, treatment, width, height)) {
        dictionary.erase("Mask");
      }
    } else if (!samples.alpha.empty() && smask.is_null()) {
      // A JPEG 2000 codestream's alpha becomes a soft mask of its own, because
      // the Flate image that replaces the codestream carries no alpha.
      PixelBuffer alpha(width, height, 1, 8, document_.budget(), what);
      for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
          const std::size_t at = (static_cast<std::size_t>(y) * width) + x;
          alpha.set(x, y, 0, at < samples.alpha.size() ? samples.alpha[at] : 255);
        }
      }
      BoxPainter::Meaning grey;
      grey.space = images::ColourSpace::device(1);
      grey.decode = {0.0, 1.0};
      grey.bits = 8;
      grey.components = 1;
      treat_mask(alpha, grey, treatment, width, height);
      Dictionary mask_dictionary;
      mask_dictionary.set("Type", Object::make_name("XObject"));
      mask_dictionary.set("Subtype", Object::make_name("Image"));
      mask_dictionary.set("Width", Object::make_integer(width));
      mask_dictionary.set("Height", Object::make_integer(height));
      mask_dictionary.set("ColorSpace", Object::make_name("DeviceGray"));
      mask_dictionary.set("BitsPerComponent", Object::make_integer(8));
      const std::vector<std::byte> packed = alpha.pack();
      dictionary.set("SMask",
                     Object::make_reference(graph_.add(Object::make_stream(
                         writer::StreamCopier::compress(std::move(mask_dictionary), packed)))));
    }
    syntax::Stream replaced =
        stream_of(Reencoder::encode(dictionary, samples.buffer, samples.codec, samples.image_mask));
    std::string digest = hex(replaced.owned_data());
    graph_.set(number, Object::make_stream(std::move(replaced)));
    return digest;
  }

  // Treats the mask object number; false when it does not decode (the
  // caller then drops it, so its shape cannot outline what was treated).
  bool treat_mask_object(std::uint32_t number, bool soft, const Treatment& treatment,
                         std::uint32_t width, std::uint32_t height) {
    const Object value = graph_.get(number);
    const syntax::Stream* stream = value.stream();
    if (stream == nullptr) {
      return false;
    }
    ImageDecoder::SamplesResult result = ImageDecoder::samples(document_, *stream, soft);
    if (!result.samples) {
      return false;
    }
    ImageDecoder::Samples& samples = *result.samples;
    treat_mask(samples.buffer, meaning_of(samples), treatment, width, height);
    graph_.set(number,
               Object::make_stream(stream_of(Reencoder::encode(
                   stream->dictionary(), samples.buffer, samples.codec, samples.image_mask))));
    return true;
  }

  // The bytes of a treated inline image's operation, or nothing (the draw
  // goes) when it no longer decodes; digest receives the SHA-256 of its data
  // as written between ID and the white space before EI.
  std::string treat_inline(const Dictionary& written, platform::ByteSpan data,
                           const Object& colour_spaces, const Treatment& treatment,
                           std::string& digest) {
    ImageDecoder::SamplesResult result =
        ImageDecoder::samples_inline(document_, written, data, colour_spaces);
    if (!result.samples) {
      return {};
    }
    ImageDecoder::Samples& samples = *result.samples;
    ImageTreatmentApplier::treat(samples, treatment);
    Dictionary full;
    for (const auto& [key, value] : written.entries()) {
      full.set(images::ImageIdentity::full_key(key), value);
    }
    // The samples are written as they are, never as a JPEG again: a JPX
    // source keeps its rule for /Decode, every other codec reads as none.
    const Reencoder::Result encoded =
        Reencoder::encode(full, samples.buffer, samples.codec == "JPXDecode" ? samples.codec : "",
                          samples.image_mask);
    const std::vector<std::byte>& bytes = encoded.data;
    static const std::map<std::string, std::string, std::less<>> abbreviations{
        {"BitsPerComponent", "BPC"}, {"ColorSpace", "CS"}, {"Decode", "D"},
        {"DecodeParms", "DP"},       {"Filter", "F"},      {"Height", "H"},
        {"ImageMask", "IM"},         {"Interpolate", "I"}, {"Width", "W"}};
    static const std::map<std::string, std::string, std::less<>> spaces{
        {"DeviceGray", "G"}, {"DeviceRGB", "RGB"}, {"DeviceCMYK", "CMYK"}};
    const writer::Serialiser::Numbering numbering =
        [](Object::Reference reference) -> std::optional<std::uint32_t> {
      return reference.number;
    };
    std::string out = "BI";
    for (const auto& [key, value] : encoded.dictionary.entries()) {
      if (key == "Filter" || key == "Length") {
        continue;
      }
      const auto short_key = abbreviations.find(key);
      out += " /" + (short_key != abbreviations.end() ? short_key->second : key) + " ";
      const std::string* name = value.name();
      const auto space = name != nullptr ? spaces.find(*name) : spaces.end();
      if (key == "ColorSpace" && space != spaces.end()) {
        out += "/" + space->second;
      } else {
        writer::Serialiser::write(out, value, numbering);
      }
    }
    out += " /F /AHx ID ";
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string text;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      const auto byte = std::to_integer<unsigned>(bytes[i]);
      text += digits[byte >> 4U];
      text += digits[byte & 0x0FU];
      if (i % 64 == 63) {
        text += '\n';
      }
    }
    text += '>';
    digest = hex(std::as_bytes(std::span(text)));
    out += text;
    out += "\nEI";
    return out;
  }

  // The device rectangles of every treated draw on the output's pages.
  void rectangles(const std::map<std::string, Treatment>& treatments) {
    std::map<std::uint32_t, std::uint32_t> output_page;
    for (std::size_t i = 0; i < report_.pages.size(); ++i) {
      output_page.emplace(report_.pages[i], static_cast<std::uint32_t>(i + 1));
    }
    for (const auto& [identity, treatment] : treatments) {
      for (const auto& [source, boxes] : treatment.rectangles) {
        const auto page = output_page.find(source);
        if (page == output_page.end()) {
          continue;
        }
        auto entry = std::ranges::find(report_.rectangles, page->second,
                                       &writer::WriteReport::Rectangles::page);
        if (entry == report_.rectangles.end()) {
          writer::WriteReport::Rectangles added;
          added.page = page->second;
          added.source_page = source;
          report_.rectangles.push_back(std::move(added));
          entry = report_.rectangles.end() - 1;
        }
        for (const text::Rect& box : boxes) {
          if (!box.is_empty()) {
            entry->boxes.push_back({box.left, box.bottom, box.right, box.top});
          }
        }
      }
    }
    std::ranges::stable_sort(report_.rectangles, {}, &writer::WriteReport::Rectangles::page);
  }

  EditPlan& plan_;
  writer::ObjectGraph& graph_;
  writer::WriteReport& report_;
  syntax::Document& document_;
  std::map<std::string, writer::WriteReport::Image> images_;
  std::map<std::string, std::string> inline_cache_;
};

}  // namespace

void ImageTreatmentApplier::treat(ImageDecoder::Samples& samples, const Treatment& treatment) {
  const BoxPainter::Meaning meaning = meaning_of(samples);
  for (const PixelBuffer::Rect& rect : treatment.blurs) {
    MosaicBlur::apply(samples.buffer, rect, treatment.block, meaning);
  }
  for (const ImageTreatmentPlan::Box& box : treatment.boxes) {
    const std::vector<std::uint16_t> fill = BoxPainter::nearest(meaning, box.colour);
    BoxPainter::paint(samples.buffer, box.pixels, fill);
    if (!box.caption.empty()) {
      const std::vector<std::uint16_t> ink = BoxPainter::contrast(meaning, fill);
      (void)CaptionDrawer::draw(samples.buffer, box.pixels, box.caption, ink,
                                treatment.orientation);
    }
  }
}

void ImageTreatmentApplier::apply(EditPlan& plan, writer::ObjectGraph& graph,
                                  writer::WriteReport& report) {
  if (plan.images().empty()) {
    return;
  }
  Stage stage(plan, graph, report);
  stage.run();
}

}  // namespace pdfkit::redact
