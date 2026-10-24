// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Budget bounds what one document may cost: bytes produced per stream and per
// document, and the number of objects. Every decoder charges the bytes it
// produces, and every size read from the file is checked before anything is
// reserved, because a header is a claim and a few kilobytes of Flate can
// inflate to gigabytes (docs/DESIGN.md section 3.2). A budget belongs to one
// document handle, so it needs no locking.

#ifndef PDFKIT_PLATFORM_BUDGET_HPP
#define PDFKIT_PLATFORM_BUDGET_HPP

#include <cstdint>
#include <string_view>

namespace pdfkit::platform {

class Budget {
 public:
  // The defaults are the limits of docs/DESIGN.md section 3.2.
  struct Limits {
    std::uint64_t stream_bytes = 512ULL * 1024 * 1024;
    std::uint64_t document_bytes = 2048ULL * 1024 * 1024;
    std::uint64_t objects = 8388607;
  };

  Budget();
  // Throws Error (invalid_argument) when a limit is zero.
  explicit Budget(const Limits& limits);

  [[nodiscard]] const Limits& limits() const noexcept { return limits_; }
  [[nodiscard]] std::uint64_t document_used() const noexcept { return document_used_; }
  [[nodiscard]] std::uint64_t objects_counted() const noexcept { return objects_counted_; }

  // Charges bytes produced by one stream. stream_used is that stream's running
  // count, held by its decoder. object names the stream, such as "object 12".
  void charge(std::uint64_t bytes, std::uint64_t& stream_used, std::string_view object);

  // Counts one more object read from the file.
  void count_object(std::string_view object);

  // Checks a size read from the file against the per-stream limit before it is
  // used to reserve memory. what names the claim, such as "the /Length of object 4".
  void check_claim(std::uint64_t claimed_bytes, std::string_view what) const;

 private:
  Limits limits_;
  std::uint64_t document_used_ = 0;
  std::uint64_t objects_counted_ = 0;
};

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_BUDGET_HPP
