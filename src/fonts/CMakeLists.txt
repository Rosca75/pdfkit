# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the fonts layer, added to pdfkit_core. A new file of this
# layer is one line in this list and touches no other layer's list.

target_sources(pdfkit_core PRIVATE
  cff_font.cpp
  cmap.cpp
  cmap_parser.cpp
  decoding_ladder.cpp
  encodings.cpp
  font_loader.cpp
  font_program.cpp
  generated/core14_table.cpp
  generated/glyph_list_table.cpp
  glyph_list.cpp
  ligatures.cpp
  pdf_font.cpp
  simple_font.cpp
  standard_fonts.cpp
  true_type_tables.cpp
  type0_font.cpp
  type1_font.cpp)
