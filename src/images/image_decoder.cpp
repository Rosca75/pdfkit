// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The decoding pipeline. An image is read in three steps: the encoded samples
// (the filter output, or a codec's output), then row by row through the
// unpacker, /Decode and the colour space into the output samples. A
// one-component image converts through a table of every raw value; a
// multi-component one through a cache of the raw values met, bounded in size,
// so a tint transform runs once per distinct colour. A CMYK JPEG's samples are
// read as libjpeg returns them, whatever its Adobe APP14 marker says: PDFium
// reads them so (measured on the dataset's CMYK JPEGs against
// FPDFImageObj_GetBitmap), and /Decode inverts them where a file means it. A
// codec's own size
// (the JPEG's, the JPEG 2000 codestream's) wins over /Width and /Height, as
// the pixels are what the codec holds; a bitonal codec's output is cropped or
// padded with white to /Width and /Height.

#include "images/image_decoder.hpp"

#include "images/ccitt_decoder.hpp"
#include "images/colour_space.hpp"
#include "images/dct_decoder.hpp"
#include "images/decode_fault.hpp"
#include "images/image_identity.hpp"
#include "images/jbig2_decoder.hpp"
#include "images/jpx_decoder.hpp"
#include "images/masks.hpp"
#include "images/sample_unpacker.hpp"
#include "platform/checked.hpp"
#include "syntax/filters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pdfkit::images {

namespace {

using syntax::Dictionary;
using syntax::Document;
using syntax::Filters;
using syntax::Object;

// What a stream is decoded as.
enum class Role : std::uint8_t { image, soft_mask, stencil_mask };

// The largest width or height accepted, as the content lexer bounds an inline
// image's computable length.
constexpr std::int64_t max_side = 1048576;
// The colour cache of a multi-component image holds at most this many colours.
constexpr std::size_t max_cached_colours = 1U << 18U;

// The value of key in dictionary, found under its full or its abbreviated
// name, resolved.
Object entry(Document& document, const Dictionary& dictionary, std::string_view key) {
  const Object* found = nullptr;
  for (const auto& [written, value] : dictionary.entries()) {
    if (ImageIdentity::full_key(written) == key) {
      found = &value;
    }
  }
  return found != nullptr ? document.resolve(*found) : Object();
}

struct Source {
  const Dictionary* dictionary = nullptr;
  Filters::Result decoded;
  std::string object;
  Object colour_spaces;
};

// The encoded samples handed to the colour pipeline.
struct Samples {
  std::vector<std::uint8_t> bytes;
  std::size_t stride = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t bits = 8;
  std::uint32_t components = 1;
  std::shared_ptr<const ColourSpace> space;
  // A JPEG 2000 codestream's alpha (SMaskInData).
  std::vector<std::uint8_t> alpha;
};

std::int64_t positive(Document& document, const Dictionary& dictionary, std::string_view key) {
  const Object value = entry(document, dictionary, key);
  std::optional<std::int64_t> number = value.integer();
  if (!number) {
    if (const std::optional<double> real = value.number()) {
      number = static_cast<std::int64_t>(std::llround(*real));
    }
  }
  if (!number || *number <= 0 || *number > max_side) {
    throw DecodeFault("the image's /" + std::string(key) + " is missing or outside 1 to " +
                      std::to_string(max_side));
  }
  return *number;
}

std::vector<double> numbers(Document& document, const Object& value) {
  std::vector<double> out;
  if (const Object::Array* list = value.array()) {
    for (const Object& item : *list) {
      out.push_back(document.resolve(item).number().value_or(0.0));
    }
  }
  return out;
}

std::uint8_t to_byte(std::byte value) {
  return std::to_integer<std::uint8_t>(value);
}

// Copies the packed rows of a bitonal codec into rows of the image's width,
// padding with white (pad) where the codec's bitmap is smaller.
std::vector<std::uint8_t> fit_rows(const std::vector<std::byte>& data, std::size_t source_stride,
                                   std::uint32_t source_rows, std::uint32_t width,
                                   std::uint32_t height, std::uint8_t pad, Document& document,
                                   std::string_view object) {
  const std::size_t stride = (static_cast<std::size_t>(width) + 7) / 8;
  const std::uint64_t size =
      platform::checked_mul<std::uint64_t>(stride, height, "the rows of a bitonal image");
  document.budget().check_claim(size, "the rows of the bitonal image in " + std::string(object));
  std::vector<std::uint8_t> out(static_cast<std::size_t>(size), pad);
  const std::size_t copy = std::min(stride, source_stride);
  for (std::uint32_t y = 0; y < height && y < source_rows; ++y) {
    const std::size_t from = static_cast<std::size_t>(y) * source_stride;
    if (from + copy > data.size()) {
      break;
    }
    for (std::size_t i = 0; i < copy; ++i) {
      out[y * stride + i] = to_byte(data[from + i]);
    }
  }
  return out;
}

class Pipeline {
 public:
  Pipeline(Document& document, Source source, Role role)
      : document_(document), source_(std::move(source)), role_(role) {}

  ImageDecoder::Result run(bool want_colour_key_alpha) {
    ImageDecoder::Result result;
    const Dictionary& dictionary = *source_.dictionary;
    image_mask_ = role_ == Role::stencil_mask ||
                  entry(document_, dictionary, "ImageMask").boolean().value_or(false);
    if (source_.decoded.damaged) {
      result.damaged = true;
    }
    Samples samples = encoded(result);
    std::vector<double> decode = numbers(document_, entry(document_, dictionary, "Decode"));
    if (decode.size() < 2 * samples.components) {
      decode =
          image_mask_ ? std::vector<double>{0.0, 1.0} : samples.space->default_decode(samples.bits);
    }
    std::vector<std::uint32_t> key;
    if (want_colour_key_alpha && !image_mask_) {
      key = Masks::colour_key(document_, entry(document_, dictionary, "Mask"), samples.components);
      if (key.empty()) {
        throw DecodeFault("the image's /Mask colour key is not two numbers per component");
      }
    }
    PixelImage image = render(samples, decode, key);
    if (want_colour_key_alpha) {
      PixelImage mask;
      mask.width = image.width;
      mask.height = image.height;
      mask.channels = 1;
      mask.bits = 1;
      mask.samples = std::move(image.alpha);
      result.image = std::move(mask);
      return result;
    }
    if (!samples.alpha.empty()) {
      alpha_ = std::move(samples.alpha);
    }
    result.image = std::move(image);
    return result;
  }

  // The JPEG 2000 alpha found by run, for /SMaskInData.
  std::vector<std::uint8_t>& alpha() { return alpha_; }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] const std::shared_ptr<const ColourSpace>& space() const noexcept { return space_; }

 private:
  Samples encoded(ImageDecoder::Result& result) {
    const Dictionary& dictionary = *source_.dictionary;
    Filters::Result& decoded = source_.decoded;
    std::string codec;
    if (!decoded.remaining.empty()) {
      codec = std::string(Filters::full_name(decoded.remaining.front()));
      if (!Filters::is_image_codec(codec)) {
        throw DecodeFault("the filter /" + codec + " is not one pdfkit decodes");
      }
      if (decoded.remaining.size() > 1) {
        throw DecodeFault("the filter /" + std::string(Filters::full_name(decoded.remaining[1])) +
                          " follows the image codec /" + codec);
      }
    }
    const Object parameters = decoded.remaining_parameters.empty()
                                  ? Object()
                                  : document_.resolve(decoded.remaining_parameters[0]);
    const Dictionary* parms = parameters.dictionary();
    const auto parm = [&](std::string_view key) {
      return parms != nullptr ? document_.resolve_entry(*parms, key) : Object();
    };
    Samples samples;
    std::shared_ptr<const ColourSpace> space;
    if (!image_mask_) {
      if (role_ == Role::soft_mask) {
        space = ColourSpace::device(1);
      } else {
        const Object stated = entry(document_, dictionary, "ColorSpace");
        if (!stated.is_null()) {
          space = ColourSpace::parse(document_, stated, source_.colour_spaces);
        } else if (codec == "CCITTFaxDecode" || codec == "JBIG2Decode") {
          space = ColourSpace::device(1);
        } else if (codec != "DCTDecode" && codec != "JPXDecode") {
          throw DecodeFault("the image has no /ColorSpace");
        }
      }
    }
    if (codec == "DCTDecode") {
      const int transform = static_cast<int>(parm("ColorTransform").integer().value_or(-1));
      DctDecoder::Result jpeg = DctDecoder::decode(platform::ByteSpan(decoded.bytes), transform,
                                                   document_.budget(), source_.object);
      if (!jpeg.fault.empty()) {
        throw DecodeFault("DCTDecode: " + jpeg.fault);
      }
      if (jpeg.warnings > 0) {
        result.damaged = true;
      }
      samples.width = jpeg.width;
      samples.height = jpeg.height;
      samples.components = jpeg.components;
      samples.bits = 8;
      samples.stride = static_cast<std::size_t>(jpeg.width) * jpeg.components;
      samples.bytes = std::move(jpeg.samples);
      if (image_mask_) {
        if (jpeg.components != 1) {
          throw DecodeFault("DCTDecode: a stencil JPEG has " + std::to_string(jpeg.components) +
                            " components");
        }
      } else if (!space || space->components() != jpeg.components) {
        space = ColourSpace::device(jpeg.components);
      }
    } else if (codec == "JPXDecode") {
      JpxDecoder::Options options;
      if (space) {
        options.components = static_cast<std::uint32_t>(space->components());
        options.raw = space->family() == ColourSpace::Family::indexed;
      } else if (image_mask_) {
        options.components = 1;
      }
      const std::int64_t in_data =
          entry(document_, dictionary, "SMaskInData").integer().value_or(0);
      options.alpha = in_data == 1 || in_data == 2;
      JpxDecoder::Result jpx = JpxDecoder::decode(platform::ByteSpan(decoded.bytes), options,
                                                  document_.budget(), source_.object);
      if (!jpx.fault.empty()) {
        throw DecodeFault("JPXDecode: " + jpx.fault);
      }
      samples.width = jpx.width;
      samples.height = jpx.height;
      samples.components = jpx.components;
      samples.bits = jpx.bits;
      samples.stride = static_cast<std::size_t>(jpx.width) * jpx.components * (jpx.bits / 8);
      samples.bytes = std::move(jpx.samples);
      samples.alpha = std::move(jpx.alpha);
      if (image_mask_) {
        if (jpx.bits != 8 || jpx.components != 1) {
          throw DecodeFault("JPXDecode: a stencil codestream is not one 8-bit component");
        }
      } else if (!space) {
        space = ColourSpace::device(jpx.components);
      }
    } else if (codec == "CCITTFaxDecode") {
      const auto width = static_cast<std::uint32_t>(positive(document_, dictionary, "Width"));
      const auto height = static_cast<std::uint32_t>(positive(document_, dictionary, "Height"));
      CcittDecoder::Parameters p;
      p.k = parm("K").integer().value_or(0);
      p.end_of_line = parm("EndOfLine").boolean().value_or(false);
      p.encoded_byte_align = parm("EncodedByteAlign").boolean().value_or(false);
      p.columns = parm("Columns").integer().value_or(1728);
      p.rows = parm("Rows").integer().value_or(height);
      p.end_of_block = parm("EndOfBlock").boolean().value_or(true);
      p.black_is_1 = parm("BlackIs1").boolean().value_or(false);
      p.damaged_rows_before_error = parm("DamagedRowsBeforeError").integer().value_or(0);
      if (p.columns <= 0 || p.columns > max_side) {
        throw DecodeFault("CCITTFaxDecode: /Columns is outside 1 to " + std::to_string(max_side));
      }
      const CcittDecoder::Result fax = CcittDecoder::decode(platform::ByteSpan(decoded.bytes), p,
                                                            document_.budget(), source_.object);
      if (!fax.fault.empty()) {
        if (fax.rows == 0) {
          throw DecodeFault("CCITTFaxDecode: " + fax.fault);
        }
        result.damaged = true;
      }
      if (fax.damaged_rows > 0 || fax.rows < height) {
        result.damaged = true;
      }
      const std::uint8_t white = p.black_is_1 ? 0x00 : 0xFF;
      samples.bytes = fit_rows(fax.data, (static_cast<std::size_t>(fax.columns) + 7) / 8, fax.rows,
                               width, height, white, document_, source_.object);
      samples.width = width;
      samples.height = height;
      samples.bits = 1;
      samples.components = 1;
      samples.stride = (static_cast<std::size_t>(width) + 7) / 8;
    } else if (codec == "JBIG2Decode") {
      const auto width = static_cast<std::uint32_t>(positive(document_, dictionary, "Width"));
      const auto height = static_cast<std::uint32_t>(positive(document_, dictionary, "Height"));
      std::vector<std::byte> globals;
      const Object globals_object = parm("JBIG2Globals");
      if (const syntax::Stream* stream = globals_object.stream()) {
        globals = document_.decode(*stream).bytes;
      }
      const Jbig2Decoder::Result page = Jbig2Decoder::decode(
          platform::ByteSpan(decoded.bytes), globals, document_.budget(), source_.object);
      if (!page.fault.empty()) {
        throw DecodeFault("JBIG2Decode: " + page.fault);
      }
      samples.bytes = fit_rows(page.data, (static_cast<std::size_t>(page.width) + 7) / 8,
                               page.height, width, height, 0xFF, document_, source_.object);
      samples.width = width;
      samples.height = height;
      samples.bits = 1;
      samples.components = 1;
      samples.stride = (static_cast<std::size_t>(width) + 7) / 8;
    } else {
      samples.width = static_cast<std::uint32_t>(positive(document_, dictionary, "Width"));
      samples.height = static_cast<std::uint32_t>(positive(document_, dictionary, "Height"));
      if (image_mask_) {
        samples.bits = 1;
      } else {
        const std::int64_t bits =
            entry(document_, dictionary, "BitsPerComponent").integer().value_or(0);
        if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) {
          throw DecodeFault("the image's /BitsPerComponent is " + std::to_string(bits) +
                            ", not 1, 2, 4, 8 or 16");
        }
        samples.bits = static_cast<std::uint32_t>(bits);
      }
      samples.components = image_mask_ ? 1U : static_cast<std::uint32_t>(space->components());
      const std::uint64_t row_bits = platform::checked_mul<std::uint64_t>(
          std::uint64_t{samples.width} * samples.components, samples.bits, "an image row");
      samples.stride = static_cast<std::size_t>((row_bits + 7) / 8);
      const std::uint64_t needed = platform::checked_mul<std::uint64_t>(
          samples.stride, samples.height, "the samples of an image");
      document_.budget().check_claim(needed, "the samples of the image in " + source_.object);
      samples.bytes.resize(decoded.bytes.size());
      std::transform(decoded.bytes.begin(), decoded.bytes.end(), samples.bytes.begin(), to_byte);
      if (samples.bytes.size() < needed) {
        if (samples.bytes.empty()) {
          throw DecodeFault("the image's data is empty");
        }
        // A short image reads its missing samples as zero, as viewers read it.
        result.damaged = true;
        samples.bytes.resize(static_cast<std::size_t>(needed), 0);
      }
    }
    if (image_mask_ && samples.components != 1) {
      throw DecodeFault("a stencil image has more than one component");
    }
    samples.space = image_mask_ ? ColourSpace::device(1) : space;
    space_ = samples.space;
    width_ = samples.width;
    height_ = samples.height;
    return samples;
  }

  PixelImage render(const Samples& samples, const std::vector<double>& decode,
                    const std::vector<std::uint32_t>& key) {
    const ColourSpace& space = *samples.space;
    const std::uint32_t components = samples.components;
    PixelImage image;
    image.width = samples.width;
    image.height = samples.height;
    image.channels = image_mask_ ? 1U : static_cast<std::uint32_t>(space.output_channels());
    const bool gray_device = space.family() == ColourSpace::Family::device_gray;
    if (image_mask_ || (samples.bits == 1 && components == 1 && gray_device)) {
      image.bits = 1;
    } else if (samples.bits == 16 && space.family() != ColourSpace::Family::indexed) {
      image.bits = 16;
    } else {
      image.bits = 8;
    }
    const std::uint64_t pixels = platform::checked_mul<std::uint64_t>(samples.width, samples.height,
                                                                      "the pixels of an image");
    const std::uint64_t out_bytes = platform::checked_mul<std::uint64_t>(
        pixels, std::uint64_t{image.channels} * image.bytes_per_sample(), "the pixels of an image");
    document_.budget().check_claim(out_bytes, "the pixels of the image in " + source_.object);
    std::uint64_t used = 0;
    document_.budget().charge(out_bytes, used, source_.object);
    image.samples.assign(static_cast<std::size_t>(out_bytes), 0);
    const bool keep_cmyk = space.is_cmyk() && samples.bits == 8 && !image_mask_;
    if (keep_cmyk) {
      document_.budget().charge(pixels * 4, used, source_.object);
      image.cmyk.assign(static_cast<std::size_t>(pixels * 4), 0);
    }
    if (!key.empty()) {
      image.alpha.assign(static_cast<std::size_t>(pixels), 255);
    }
    SampleUnpacker unpacker(samples.bits, static_cast<std::size_t>(samples.width) * components,
                            components, decode);
    const double out_max = image.bits == 16 ? 65535.0 : 255.0;
    const std::size_t channels = image.channels;
    const std::size_t sample_bytes = image.bytes_per_sample();
    // One-component spaces convert through a table of every raw value.
    std::vector<std::uint16_t> table;
    if (components == 1 && !image_mask_) {
      table.resize((static_cast<std::size_t>(unpacker.max_value()) + 1) * channels);
      std::array<double, 3> out{};
      for (std::uint32_t raw = 0; raw <= unpacker.max_value(); ++raw) {
        const std::array<double, 1> in{unpacker.decoded(raw, 0)};
        space.convert(in, std::span<double>(out.data(), channels));
        for (std::size_t c = 0; c < channels; ++c) {
          table[raw * channels + c] =
              static_cast<std::uint16_t>(std::lround(std::clamp(out[c], 0.0, 1.0) * out_max));
        }
      }
    }
    std::unordered_map<std::uint64_t, std::array<std::uint16_t, 3>> cache;
    std::vector<std::uint16_t> raw(static_cast<std::size_t>(samples.width) * components);
    std::array<double, 32> in{};
    std::array<double, 3> out{};
    for (std::uint32_t y = 0; y < samples.height; ++y) {
      const std::size_t start = static_cast<std::size_t>(y) * samples.stride;
      const std::size_t end = std::min(start + samples.stride, samples.bytes.size());
      const platform::ByteSpan row =
          start < end ? std::as_bytes(std::span(samples.bytes.data() + start, end - start))
                      : platform::ByteSpan();
      unpacker.unpack(row, raw);
      for (std::uint32_t x = 0; x < samples.width; ++x) {
        const std::size_t pixel = static_cast<std::size_t>(y) * samples.width + x;
        const std::span<const std::uint16_t> values(raw.data() + x * components, components);
        if (!key.empty() && Masks::keyed(values, key)) {
          image.alpha[pixel] = 0;
        }
        std::uint8_t* target = image.samples.data() + pixel * channels * sample_bytes;
        if (image_mask_) {
          // A 0 after /Decode paints (section 8.9.6.2, recalled).
          const bool painted = unpacker.decoded(values[0], 0) < 0.5;
          if (role_ == Role::stencil_mask) {
            target[0] = painted ? 255 : 0;
          } else {
            target[0] = painted ? 0 : 255;
          }
          continue;
        }
        const std::uint16_t* result = nullptr;
        std::array<std::uint16_t, 3> converted{};
        if (!table.empty()) {
          result = table.data() + static_cast<std::size_t>(values[0]) * channels;
        } else {
          std::uint64_t cache_key = 0;
          const bool cacheable = components <= 4;
          if (cacheable) {
            for (const std::uint16_t v : values) {
              cache_key = (cache_key << 16U) | v;
            }
            const auto found = cache.find(cache_key);
            if (found != cache.end()) {
              converted = found->second;
              result = converted.data();
            }
          }
          if (result == nullptr) {
            for (std::size_t c = 0; c < components && c < in.size(); ++c) {
              in[c] = unpacker.decoded(values[c], c);
            }
            space.convert(std::span<const double>(in.data(), std::min<std::size_t>(components, 32)),
                          std::span<double>(out.data(), channels));
            for (std::size_t c = 0; c < channels; ++c) {
              converted[c] =
                  static_cast<std::uint16_t>(std::lround(std::clamp(out[c], 0.0, 1.0) * out_max));
            }
            if (cacheable) {
              if (cache.size() >= max_cached_colours) {
                cache.clear();
              }
              cache.emplace(cache_key, converted);
            }
            result = converted.data();
          }
        }
        for (std::size_t c = 0; c < channels; ++c) {
          if (sample_bytes == 2) {
            target[2 * c] = static_cast<std::uint8_t>(result[c] >> 8U);
            target[2 * c + 1] = static_cast<std::uint8_t>(result[c] & 0xFFU);
          } else {
            target[c] = static_cast<std::uint8_t>(result[c]);
          }
        }
        if (keep_cmyk) {
          for (std::size_t c = 0; c < 4; ++c) {
            image.cmyk[pixel * 4 + c] = static_cast<std::uint8_t>(
                std::lround(std::clamp(unpacker.decoded(values[c], c), 0.0, 1.0) * 255.0));
          }
        }
      }
    }
    if (image_mask_ && role_ == Role::image) {
      image.transparent = 255;
    }
    return image;
  }

  Document& document_;
  Source source_;
  Role role_;
  bool image_mask_ = false;
  std::vector<std::uint8_t> alpha_;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::shared_ptr<const ColourSpace> space_;
};

Source stream_source(Document& document, const syntax::Stream& stream) {
  Source source;
  source.dictionary = &stream.dictionary();
  source.decoded = document.decode(stream);
  source.object = stream.describe();
  return source;
}

ImageDecoder::Result faulted(const DecodeFault& fault) {
  ImageDecoder::Result result;
  result.fault = fault.what();
  return result;
}

// The base image, with /Matte undone when its soft mask has one.
ImageDecoder::Result decode_base(Document& document, Source source) {
  const Dictionary& dictionary = *source.dictionary;
  Pipeline pipeline(document, std::move(source), Role::image);
  ImageDecoder::Result result = pipeline.run(false);
  const Object smask = entry(document, dictionary, "SMask");
  if (const syntax::Stream* mask = smask.stream(); mask != nullptr && result.image) {
    const std::vector<double> matte =
        numbers(document, document.resolve_entry(mask->dictionary(), "Matte"));
    if (!matte.empty() && pipeline.space()) {
      Pipeline soft(document, stream_source(document, *mask), Role::soft_mask);
      const ImageDecoder::Result alpha = soft.run(false);
      if (alpha.image) {
        std::array<double, 3> converted{};
        const std::size_t channels = pipeline.space()->output_channels();
        pipeline.space()->convert(matte, std::span<double>(converted.data(), channels));
        Masks::unmatte(*result.image, *alpha.image,
                       std::span<const double>(converted.data(), channels));
      }
    }
  }
  return result;
}

ImageDecoder::Result decode_mask(Document& document, Source source) {
  const Dictionary& dictionary = *source.dictionary;
  const bool jpx = ImageDecoder::encoding(document, dictionary) == "JPXDecode";
  switch (Masks::kind(document, dictionary, jpx)) {
    case Masks::Kind::soft: {
      const Object smask = entry(document, dictionary, "SMask");
      Pipeline soft(document, stream_source(document, *smask.stream()), Role::soft_mask);
      return soft.run(false);
    }
    case Masks::Kind::stencil: {
      const Object mask = entry(document, dictionary, "Mask");
      Pipeline stencil(document, stream_source(document, *mask.stream()), Role::stencil_mask);
      return stencil.run(false);
    }
    case Masks::Kind::colour_key: {
      Pipeline keyed(document, std::move(source), Role::image);
      return keyed.run(true);
    }
    case Masks::Kind::in_data: {
      Pipeline base(document, std::move(source), Role::image);
      ImageDecoder::Result result = base.run(false);
      if (base.alpha().empty()) {
        throw DecodeFault("the JPEG 2000 codestream holds no component for /SMaskInData");
      }
      PixelImage mask;
      mask.width = base.width();
      mask.height = base.height();
      mask.channels = 1;
      mask.bits = 8;
      mask.samples = std::move(base.alpha());
      result.image = std::move(mask);
      return result;
    }
    case Masks::Kind::none:
      break;
  }
  ImageDecoder::Result result;
  result.no_mask = true;
  result.fault = "the image has no mask (no /SMask, /Mask or /SMaskInData)";
  return result;
}

ImageDecoder::Result dispatch(Document& document, Source source, ImageDecoder::Part part) {
  try {
    return part == ImageDecoder::Part::base ? decode_base(document, std::move(source))
                                            : decode_mask(document, std::move(source));
  } catch (const DecodeFault& fault) {
    return faulted(fault);
  }
}

}  // namespace

ImageDecoder::Result ImageDecoder::decode(Document& document, const syntax::Stream& stream,
                                          Part part) {
  return dispatch(document, stream_source(document, stream), part);
}

ImageDecoder::Result ImageDecoder::decode_inline(Document& document, const Dictionary& dictionary,
                                                 platform::ByteSpan data,
                                                 const Object& colour_spaces, Part part) {
  Source source;
  source.dictionary = &dictionary;
  source.object = "an inline image";
  source.colour_spaces = colour_spaces;
  Object parameters = entry(document, dictionary, "DecodeParms");
  source.decoded = Filters::decode(data, entry(document, dictionary, "Filter"), parameters,
                                   document.budget(), source.object);
  return dispatch(document, std::move(source), part);
}

std::string ImageDecoder::encoding(Document& document, const Dictionary& dictionary) {
  const Object filter = entry(document, dictionary, "Filter");
  if (const std::string* name = filter.name()) {
    return std::string(Filters::full_name(*name));
  }
  if (const Object::Array* list = filter.array(); list != nullptr && !list->empty()) {
    const Object last = document.resolve(list->back());
    if (const std::string* name = last.name()) {
      return std::string(Filters::full_name(*name));
    }
  }
  return "none";
}

}  // namespace pdfkit::images
