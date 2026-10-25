// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The C++ side of the DCT decoder: the buffer callback, which checks the
// claimed size against the budget and keeps any refusal to rethrow once the C
// guard has returned, so no exception ever unwinds through libjpeg's frames.

#include "images/dct_decoder.hpp"

#include "platform/checked.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <limits>

namespace pdfkit::images {

using DctReserve = unsigned char* (*)(void*, unsigned, unsigned, unsigned);

// The guard of dct_guard.c.
extern "C" int pdfkit_dct_decode(const unsigned char* data, std::size_t size, long max_memory,
                                 int color_transform, DctReserve reserve, void* context, int* adobe,
                                 unsigned* warnings, char* message, std::size_t message_size);

namespace {

struct Context {
  platform::Budget* budget = nullptr;
  std::string_view object;
  DctDecoder::Result* result = nullptr;
  std::exception_ptr error;
};

unsigned char* reserve_samples(void* opaque, unsigned width, unsigned height, unsigned components) {
  auto* context = static_cast<Context*>(opaque);
  try {
    const auto pixels =
        platform::checked_mul<std::uint64_t>(width, height, "the pixel count of a JPEG");
    const auto bytes =
        platform::checked_mul<std::uint64_t>(pixels, components, "the samples of a JPEG");
    context->budget->check_claim(bytes,
                                 "the samples of the JPEG in " + std::string(context->object));
    std::uint64_t used = 0;
    context->budget->charge(bytes, used, context->object);
    context->result->samples.assign(static_cast<std::size_t>(bytes), 0);
    context->result->width = width;
    context->result->height = height;
    context->result->components = components;
    return context->result->samples.data();
  } catch (...) {
    context->error = std::current_exception();
    return nullptr;
  }
}

}  // namespace

DctDecoder::Result DctDecoder::decode(platform::ByteSpan data, int color_transform,
                                      platform::Budget& budget, std::string_view object) {
  Result result;
  Context context{&budget, object, &result, nullptr};
  int adobe = 0;
  unsigned warnings = 0;
  std::array<char, 256> message{};
  const auto max_memory = static_cast<long>(std::min<std::uint64_t>(
      budget.limits().stream_bytes, static_cast<std::uint64_t>(std::numeric_limits<long>::max())));
  const int status = pdfkit_dct_decode(reinterpret_cast<const unsigned char*>(data.data()),
                                       data.size(), max_memory, color_transform, reserve_samples,
                                       &context, &adobe, &warnings, message.data(), message.size());
  result.adobe = adobe != 0;
  result.warnings = warnings;
  if (context.error) {
    std::rethrow_exception(context.error);
  }
  if (status != 0) {
    result.fault = std::string("libjpeg-turbo: ") + message.data();
    result.samples.clear();
  }
  return result;
}

}  // namespace pdfkit::images
