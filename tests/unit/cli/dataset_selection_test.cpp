// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// Tests of the dataset verb's seeded page selection against pinned draws. The
// writer gate repeats the same draw in Python (its selection_pages, pinned to
// the same values by SelectionPagesTest in tests/dataset/test_phase7.py) to
// count the kept pages of a selection the run never wrote, so a change to
// either side fails a test until both agree.

#include "cli/verbs/dataset_write.hpp"

#include <doctest.h>

#include <cstdint>
#include <vector>

namespace {

using pdfkit::cli::verbs::dataset_selection;

TEST_CASE("the dataset selection keeps the pages the outcome contract draws") {
  CHECK(dataset_selection("a-0002", 3) == std::vector<std::uint64_t>{2, 3});
  CHECK(dataset_selection("a-0001", 2) == std::vector<std::uint64_t>{2});
  CHECK(dataset_selection("qpdf-0001", 7) == std::vector<std::uint64_t>{2, 1});
  const std::vector<std::uint64_t> long_file = dataset_selection("dccc-0129", 394);
  REQUIRE(long_file.size() == 200);
  CHECK(std::vector<std::uint64_t>(long_file.begin(), long_file.begin() + 5) ==
        std::vector<std::uint64_t>{141, 318, 136, 243, 207});
}

}  // namespace
