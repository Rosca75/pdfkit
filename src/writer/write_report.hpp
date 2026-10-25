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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pdfkit::writer {

class WriteReport {
 public:
  std::vector<std::uint32_t> pages;
  std::size_t bytes = 0;
  std::string sha256;
  std::size_t objects = 0;
  std::vector<ChannelTable::Count> channels;
  std::size_t pending_redactions = 0;
  std::size_t missing_references = 0;
  std::size_t unpruned_resources = 0;
  // "not_run", or the self-check's verdict once it has run.
  std::string self_check_status = "not_run";
  std::string self_check_reason;
  std::vector<ChannelTable::Warning> warnings;

  [[nodiscard]] std::string json() const;
};

}  // namespace pdfkit::writer

#endif  // PDFKIT_WRITER_WRITE_REPORT_HPP
