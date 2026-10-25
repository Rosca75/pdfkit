// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// WriteReport is what pdfkit_write reports beside the bytes (docs/ABI.md, the
// pdfkit_write section): the page count and the kept source pages, the size
// and SHA-256 of the output, per channel of the table the occurrences
// stripped, the /Redact annotations never applied, the references written as
// null, the resource dictionaries kept whole, the rectangles changed, the
// self-check's result and the warnings met. Keys are written in a fixed
// order, so one write gives one report.

#ifndef PDFKIT_WRITER_WRITE_REPORT_HPP
#define PDFKIT_WRITER_WRITE_REPORT_HPP

#include "writer/channel_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pdfkit::writer {

class WriteReport {
 public:
  // The device rectangles a text redaction changed on one output page
  // ([left, bottom, right, top] in default user space), and the placeholders
  // it drew there.
  struct Rectangles {
    std::uint32_t page = 0;
    std::uint32_t source_page = 0;
    std::vector<std::array<double, 4>> boxes;
    std::size_t placeholders = 0;
  };

  // What the image-treatment stage did to one image identity: its
  // treatment, the image objects replaced or removed, the inline draws
  // rewritten or removed, the draws removed, and the SHA-256 of each
  // treated object's new encoded bytes, in object order.
  struct Image {
    std::string identity;
    std::string treatment;
    std::size_t objects = 0;
    std::size_t inline_draws = 0;
    std::size_t draws_removed = 0;
    std::vector<std::string> sha256;
  };

  std::vector<std::uint32_t> pages;
  std::size_t bytes = 0;
  std::string sha256;
  std::size_t objects = 0;
  std::vector<ChannelTable::Count> channels;
  std::size_t pending_redactions = 0;
  std::size_t missing_references = 0;
  std::size_t unpruned_resources = 0;
  std::vector<Rectangles> rectangles;
  // Per treated image identity, in identity order; empty without a treatment.
  std::vector<Image> images;
  // "not_run", or the self-check's verdict once it has run.
  std::string self_check_status = "not_run";
  std::string self_check_reason;
  // The strings the self-check searched for, when it ran.
  std::size_t self_check_strings = 0;
  std::vector<ChannelTable::Warning> warnings;

  [[nodiscard]] std::string json() const;
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_WRITE_REPORT_HPP
