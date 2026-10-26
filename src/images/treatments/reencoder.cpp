// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The dictionary rewrite and the JPEG call. The libjpeg-turbo compression
// runs inside the C guard of jpeg_guard.c, which hands the compressed bytes
// back through a callback; the callback copies them and catches everything,
// because no exception may unwind through the C frame.

#include "images/treatments/reencoder.hpp"

#include "platform/error.hpp"
#include "syntax/object.hpp"

#include <array>
#include <exception>
#include <string>
#include <utility>

namespace pdfkit::images::treatments {

using JpegDeliver = int (*)(void*, const unsigned char*, std::size_t);

extern "C" int pdfkit_jpeg_encode(const unsigned char* samples, unsigned width, unsigned height,
                                  unsigned components, int quality, JpegDeliver deliver,
                                  void* context, char* message, std::size_t message_size);

namespace {

using syntax::Dictionary;
using syntax::Object;

struct Delivery {
  std::vector<std::byte>* out = nullptr;
  std::exception_ptr error;
};

int deliver(void* opaque, const unsigned char* data, std::size_t size) {
  auto* delivery = static_cast<Delivery*>(opaque);
  try {
    const auto* first = reinterpret_cast<const std::byte*>(data);
    delivery->out->assign(first, first + size);
    return 0;
  } catch (...) {
    delivery->error = std::current_exception();
    return 1;
  }
}

std::string_view device_space(std::uint32_t components) {
  if (components == 4) {
    return "DeviceCMYK";
  }
  return components == 3 ? "DeviceRGB" : "DeviceGray";
}

}  // namespace

std::vector<std::byte> Reencoder::jpeg(const PixelBuffer& samples, int quality) {
  const std::uint32_t components = samples.components();
  if (samples.bits() != 8 || (components != 1 && components != 3 && components != 4)) {
    throw platform::Error(platform::ErrorCode::internal,
                          "a treated JPEG has " + std::to_string(components) + " components of " +
                              std::to_string(samples.bits()) + " bits",
                          "8-bit samples of 1, 3 or 4 components",
                          "this is a defect in pdfkit; report it with the input file");
  }
  const std::vector<std::byte> packed = samples.pack();
  std::vector<std::byte> out;
  Delivery delivery{&out, nullptr};
  std::array<char, 256> message{};
  const int status = pdfkit_jpeg_encode(reinterpret_cast<const unsigned char*>(packed.data()),
                                        samples.width(), samples.height(), components, quality,
                                        deliver, &delivery, message.data(), message.size());
  if (delivery.error) {
    std::rethrow_exception(delivery.error);
  }
  if (status != 0) {
    throw platform::Error(
        platform::ErrorCode::internal,
        std::string("libjpeg-turbo could not compress a treated image: ") + message.data(),
        "the JPEG compressor to take 8-bit samples",
        "this is a defect in pdfkit; report it with the input file");
  }
  return out;
}

Reencoder::Result Reencoder::encode(const Dictionary& original, const PixelBuffer& samples,
                                    std::string_view codec, bool image_mask) {
  Result result;
  Dictionary dictionary = original;
  for (const std::string_view key : {"Filter", "DecodeParms", "Length", "DL", "SMaskInData"}) {
    dictionary.erase(key);
  }
  dictionary.set("Width", Object::make_integer(samples.width()));
  dictionary.set("Height", Object::make_integer(samples.height()));
  dictionary.set("BitsPerComponent", Object::make_integer(samples.bits()));
  if (!image_mask && !dictionary.contains("ColorSpace")) {
    dictionary.set("ColorSpace",
                   Object::make_name(std::string(device_space(samples.components()))));
  }
  if (codec == "JPXDecode" && !image_mask) {
    dictionary.erase("Decode");
  }
  if (codec == "DCTDecode") {
    dictionary.set("Filter", Object::make_name("DCTDecode"));
    result.data = jpeg(samples, jpeg_quality);
    result.jpeg = true;
  } else {
    result.data = samples.pack();
  }
  result.dictionary = std::move(dictionary);
  return result;
}

}  // namespace pdfkit::images::treatments
