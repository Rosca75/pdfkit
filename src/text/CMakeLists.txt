# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the text layer, added to pdfkit_core. A new file of this
# layer is one line in this list and touches no other layer's list.

target_sources(pdfkit_core PRIVATE
  colour.cpp
  content_interpreter.cpp
  content_serialiser.cpp
  glyph_record.cpp
  graphics_state.cpp
  matrix.cpp
  optional_content.cpp
  page_content.cpp
  rect.cpp
  string_operands.cpp
  text_string.cpp)
