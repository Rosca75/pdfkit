// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The limit checks of platform::Budget and the messages they refuse with, each
// naming the object, the limit and the open option that raises it.

#include "platform/budget.hpp"

#include "platform/error.hpp"

#include <string>

namespace pdfkit::platform {

namespace {

[[noreturn]] void refuse(std::string failed, std::string expected, std::string_view option) {
  throw Error(ErrorCode::limit_exceeded, std::move(failed), std::move(expected),
              "re-save or split the file with its producing application, or raise " +
                  std::string(option) + " in the open options");
}

}  // namespace

Budget::Budget() : Budget(Limits{}) {}

Budget::Budget(const Limits& limits) : limits_(limits) {
  if (limits_.stream_bytes == 0 || limits_.document_bytes == 0 || limits_.objects == 0) {
    throw Error(ErrorCode::invalid_argument, "a decoding limit was set to zero",
                "stream_bytes, document_bytes and objects each at least 1",
                "leave a limit out of the open options to use its default, or give a positive "
                "value");
  }
}

void Budget::charge(std::uint64_t bytes, std::uint64_t& stream_used, std::string_view object) {
  if (stream_used > limits_.stream_bytes || bytes > limits_.stream_bytes - stream_used) {
    refuse(std::string(object) + " produces more than " + std::to_string(limits_.stream_bytes) +
               " bytes when decoded",
           "a stream within the per-stream limit of " + std::to_string(limits_.stream_bytes) +
               " bytes",
           "stream_bytes");
  }
  if (bytes > limits_.document_bytes - document_used_) {
    refuse("decoding " + std::string(object) + " takes the document past " +
               std::to_string(limits_.document_bytes) + " decoded bytes",
           "a document within the per-document limit of " + std::to_string(limits_.document_bytes) +
               " bytes",
           "document_bytes");
  }
  stream_used += bytes;
  document_used_ += bytes;
}

void Budget::count_object(std::string_view object) {
  if (objects_counted_ >= limits_.objects) {
    refuse(std::string(object) + " takes the document past " + std::to_string(limits_.objects) +
               " objects",
           "at most " + std::to_string(limits_.objects) + " objects", "objects");
  }
  ++objects_counted_;
}

void Budget::check_claim(std::uint64_t claimed_bytes, std::string_view what) const {
  if (claimed_bytes > limits_.stream_bytes) {
    refuse(
        std::string(what) + " claims " + std::to_string(claimed_bytes) + " bytes",
        "a size within the per-stream limit of " + std::to_string(limits_.stream_bytes) + " bytes",
        "stream_bytes");
  }
}

}  // namespace pdfkit::platform
