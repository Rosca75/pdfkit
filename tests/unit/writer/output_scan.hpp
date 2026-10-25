// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The helpers the writer tests share: writing a document back through the
// writer (with nothing edited, or after a page selection), and searching an
// output for a planted marker the way an extractor would, in the raw bytes
// and in every stream decoded, because a marker found in either is a leak.

#ifndef PDFKIT_TESTS_WRITER_OUTPUT_SCAN_HPP
#define PDFKIT_TESTS_WRITER_OUTPUT_SCAN_HPP

#include "syntax/document.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pdfkit::test {

// The output of the identity write of file, through the C ABI.
[[nodiscard]] std::string identity_write(const std::string& file);

// The output of writing file with only pages kept, through the C ABI.
[[nodiscard]] std::string select_write(const std::string& file,
                                       const std::vector<std::uint32_t>& pages);

// The write report of the last identity_write or select_write of file.
[[nodiscard]] std::string write_report(const std::string& file,
                                       const std::vector<std::uint32_t>& pages = {});

// The raw bytes of output and every stream of it decoded, joined; a stream
// that does not decode contributes its raw bytes only.
[[nodiscard]] std::string everything(const std::string& output);

// Whether marker occurs in the raw bytes or in a decoded stream of output.
[[nodiscard]] bool contains_marker(const std::string& output, std::string_view marker);

// The document of output, opened; the open must need no repair.
[[nodiscard]] std::unique_ptr<syntax::Document> reopen(const std::string& output);

}  // namespace pdfkit::test

#endif  // PDFKIT_TESTS_WRITER_OUTPUT_SCAN_HPP
