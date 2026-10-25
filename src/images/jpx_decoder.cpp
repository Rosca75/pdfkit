// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The OpenJPEG adapter: a memory stream over the encoded bytes, the codec
// chosen by signature (JP2: the 12-byte signature box 0000000C 6A502020 0D0A870A;
// J2K: the SOC and SIZ markers FF4F FF51; ISO/IEC 15444-1, recalled), the
// header read, the area checked against the budget, the tiles decoded, and the
// components mapped to interleaved samples. Every OpenJPEG object is held by a
// unique_ptr whose deleter is OpenJPEG's own destroy function, so every path
// releases it; OpenJPEG reports errors by return value, never by a jump.

#include "images/jpx_decoder.hpp"

#include "platform/checked.hpp"

#include <openjpeg.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>

namespace pdfkit::images {

namespace {

struct Source {
  platform::ByteSpan data;
  std::size_t position = 0;
};

OPJ_SIZE_T read_source(void* buffer, OPJ_SIZE_T size, void* user) {
  auto* source = static_cast<Source*>(user);
  if (source->position >= source->data.size()) {
    return static_cast<OPJ_SIZE_T>(-1);
  }
  const std::size_t count = std::min<std::size_t>(size, source->data.size() - source->position);
  std::memcpy(buffer, source->data.data() + source->position, count);
  source->position += count;
  return count;
}

OPJ_OFF_T skip_source(OPJ_OFF_T count, void* user) {
  auto* source = static_cast<Source*>(user);
  const auto remaining = static_cast<OPJ_OFF_T>(source->data.size() - source->position);
  if (count < 0) {
    return -1;
  }
  const OPJ_OFF_T skipped = std::min(count, remaining);
  source->position += static_cast<std::size_t>(skipped);
  return skipped;
}

OPJ_BOOL seek_source(OPJ_OFF_T position, void* user) {
  auto* source = static_cast<Source*>(user);
  if (position < 0 || static_cast<std::uint64_t>(position) > source->data.size()) {
    return OPJ_FALSE;
  }
  source->position = static_cast<std::size_t>(position);
  return OPJ_TRUE;
}

void keep_error(const char* message, void* user) {
  auto* text = static_cast<std::string*>(user);
  *text = message != nullptr ? message : "";
  while (!text->empty() && (text->back() == '\n' || text->back() == '\r')) {
    text->pop_back();
  }
}

void ignore_message(const char* /*message*/, void* /*user*/) {}

struct CodecDeleter {
  void operator()(opj_codec_t* codec) const { opj_destroy_codec(codec); }
};
struct StreamDeleter {
  void operator()(opj_stream_t* stream) const { opj_stream_destroy(stream); }
};
struct ImageDeleter {
  void operator()(opj_image_t* image) const { opj_image_destroy(image); }
};

bool starts_with(platform::ByteSpan data, std::initializer_list<unsigned> bytes) {
  if (data.size() < bytes.size()) {
    return false;
  }
  std::size_t i = 0;
  for (const unsigned value : bytes) {
    if (std::to_integer<unsigned>(data[i++]) != value) {
      return false;
    }
  }
  return true;
}

// The value of a component's sample scaled to bits, or raw.
std::uint32_t scale(std::int32_t value, const opj_image_comp_t& component, std::uint32_t bits,
                    bool raw) {
  std::int64_t v = value;
  if (component.sgnd != 0) {
    v += std::int64_t{1} << (component.prec - 1);
  }
  const std::int64_t max_in = (std::int64_t{1} << component.prec) - 1;
  v = std::clamp<std::int64_t>(v, 0, max_in);
  const std::int64_t max_out = (std::int64_t{1} << bits) - 1;
  if (raw) {
    return static_cast<std::uint32_t>(std::min(v, max_out));
  }
  if (component.prec == bits) {
    return static_cast<std::uint32_t>(v);
  }
  return static_cast<std::uint32_t>((v * max_out + max_in / 2) / max_in);
}

}  // namespace

JpxDecoder::Result JpxDecoder::decode(platform::ByteSpan data, const Options& options,
                                      platform::Budget& budget, std::string_view object) {
  Result result;
  OPJ_CODEC_FORMAT format = OPJ_CODEC_UNKNOWN;
  if (starts_with(data, {0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20})) {
    format = OPJ_CODEC_JP2;
  } else if (starts_with(data, {0xFF, 0x4F, 0xFF, 0x51})) {
    format = OPJ_CODEC_J2K;
  } else {
    result.fault = "the JPEG 2000 data starts with neither a JP2 signature nor a codestream";
    return result;
  }
  std::string error;
  const std::unique_ptr<opj_codec_t, CodecDeleter> codec(opj_create_decompress(format));
  if (!codec) {
    result.fault = "OpenJPEG could not create a decoder";
    return result;
  }
  opj_set_info_handler(codec.get(), ignore_message, nullptr);
  opj_set_warning_handler(codec.get(), ignore_message, nullptr);
  opj_set_error_handler(codec.get(), keep_error, &error);
  opj_dparameters_t parameters;
  opj_set_default_decoder_parameters(&parameters);
  if (opj_setup_decoder(codec.get(), &parameters) == OPJ_FALSE) {
    result.fault = "OpenJPEG: " + error;
    return result;
  }
  Source source{data, 0};
  const std::unique_ptr<opj_stream_t, StreamDeleter> stream(opj_stream_create(1U << 16U, OPJ_TRUE));
  if (!stream) {
    result.fault = "OpenJPEG could not create a stream";
    return result;
  }
  opj_stream_set_read_function(stream.get(), read_source);
  opj_stream_set_skip_function(stream.get(), skip_source);
  opj_stream_set_seek_function(stream.get(), seek_source);
  opj_stream_set_user_data(stream.get(), &source, nullptr);
  opj_stream_set_user_data_length(stream.get(), data.size());
  opj_image_t* header = nullptr;
  if (opj_read_header(stream.get(), codec.get(), &header) == OPJ_FALSE) {
    const std::unique_ptr<opj_image_t, ImageDeleter> discard(header);
    result.fault = "OpenJPEG: " + (error.empty() ? std::string("the header does not read") : error);
    return result;
  }
  const std::unique_ptr<opj_image_t, ImageDeleter> image(header);
  if (image->numcomps == 0 || image->x1 <= image->x0 || image->y1 <= image->y0) {
    result.fault = "the JPEG 2000 image has no components or no area";
    return result;
  }
  const std::uint32_t width = image->x1 - image->x0;
  const std::uint32_t height = image->y1 - image->y0;
  const std::uint64_t pixels =
      platform::checked_mul<std::uint64_t>(width, height, "the area of a JPEG 2000 image");
  budget.check_claim(
      platform::checked_mul<std::uint64_t>(pixels, std::uint64_t{image->numcomps} * 4U,
                                           "the samples of a JPEG 2000 image"),
      "the samples of the JPEG 2000 image in " + std::string(object));
  if (opj_decode(codec.get(), stream.get(), image.get()) == OPJ_FALSE ||
      opj_end_decompress(codec.get(), stream.get()) == OPJ_FALSE) {
    result.fault = "OpenJPEG: " + (error.empty() ? std::string("the tiles do not decode") : error);
    return result;
  }
  const std::uint32_t count = image->numcomps;
  for (std::uint32_t i = 0; i < count; ++i) {
    const opj_image_comp_t& component = image->comps[i];
    if (component.data == nullptr || component.w == 0 || component.h == 0 || component.prec == 0 ||
        component.prec > 31 || component.dx == 0 || component.dy == 0) {
      result.fault = "the JPEG 2000 component " + std::to_string(i) + " decoded to nothing";
      return result;
    }
  }
  // The colour components: what the PDF expects, else the codestream's own.
  std::uint32_t colour = options.components;
  if (colour == 0) {
    if (count >= 4 && image->color_space == OPJ_CLRSPC_CMYK) {
      colour = 4;
    } else if (count >= 3) {
      colour = 3;
    } else {
      colour = 1;
    }
  }
  if (count < colour) {
    result.fault = "the JPEG 2000 image has " + std::to_string(count) + " components where " +
                   std::to_string(colour) + " are needed";
    return result;
  }
  std::uint32_t precision = 0;
  for (std::uint32_t i = 0; i < colour; ++i) {
    precision = std::max(precision, image->comps[i].prec);
  }
  const std::uint32_t bits = precision > 8 && !options.raw ? 16U : 8U;
  const std::size_t bytes = bits / 8U;
  const std::uint64_t total = platform::checked_mul<std::uint64_t>(
      pixels, std::uint64_t{colour} * bytes, "the samples of a JPEG 2000 image");
  std::uint64_t used = 0;
  budget.charge(total, used, object);
  result.samples.assign(static_cast<std::size_t>(total), 0);
  const bool sycc = image->color_space == OPJ_CLRSPC_SYCC && colour == 3 && !options.raw;
  const auto sample = [&](std::uint32_t c, std::uint32_t x, std::uint32_t y) -> std::int32_t {
    const opj_image_comp_t& component = image->comps[c];
    const std::uint32_t cx = std::min(component.w - 1, x / component.dx);
    const std::uint32_t cy = std::min(component.h - 1, y / component.dy);
    return component.data[static_cast<std::size_t>(cy) * component.w + cx];
  };
  std::size_t out = 0;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      if (sycc) {
        const opj_image_comp_t& luma = image->comps[0];
        const double half = std::ldexp(1.0, static_cast<int>(luma.prec) - 1);
        // Signed samples are centred on zero already; unsigned chroma on half.
        const double lv = sample(0, x, y) + (luma.sgnd != 0 ? half : 0.0);
        const double cb = sample(1, x, y) - (luma.sgnd != 0 ? 0.0 : half);
        const double cr = sample(2, x, y) - (luma.sgnd != 0 ? 0.0 : half);
        const double rgb[3] = {lv + 1.402 * cr, lv - 0.344136 * cb - 0.714136 * cr,
                               lv + 1.772 * cb};
        const double max_in = std::ldexp(1.0, static_cast<int>(luma.prec)) - 1.0;
        const double max_out = std::ldexp(1.0, static_cast<int>(bits)) - 1.0;
        for (const double value : rgb) {
          const auto v = static_cast<std::uint32_t>(
              std::lround(std::clamp(value, 0.0, max_in) * max_out / max_in));
          if (bytes == 2) {
            result.samples[out++] = static_cast<std::uint8_t>(v >> 8U);
          }
          result.samples[out++] = static_cast<std::uint8_t>(v & 0xFFU);
        }
        continue;
      }
      for (std::uint32_t c = 0; c < colour; ++c) {
        const std::uint32_t v = scale(sample(c, x, y), image->comps[c], bits, options.raw);
        if (bytes == 2) {
          result.samples[out++] = static_cast<std::uint8_t>(v >> 8U);
        }
        result.samples[out++] = static_cast<std::uint8_t>(v & 0xFFU);
      }
    }
  }
  if (options.alpha && count > colour) {
    used = 0;
    budget.charge(pixels, used, object);
    result.alpha.assign(static_cast<std::size_t>(pixels), 0);
    std::size_t index = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        result.alpha[index++] =
            static_cast<std::uint8_t>(scale(sample(colour, x, y), image->comps[colour], 8, false));
      }
    }
  }
  result.width = width;
  result.height = height;
  result.components = colour;
  result.bits = bits;
  return result;
}

}  // namespace pdfkit::images
