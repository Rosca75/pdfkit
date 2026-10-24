# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the platform layer, added to pdfkit_core. This layer is the
# only place for compiler- and OS-specific code; every other layer builds on
# its byte, error, budget, checked-arithmetic and JSON types.

target_sources(pdfkit_core PRIVATE
  budget.cpp
  buffer.cpp
  build_info.cpp
  error.cpp
  json_reader.cpp
  json_value.cpp
  json_writer.cpp
  utf8_sequence.cpp)
