# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the images layer, added to pdfkit_core. A new file of this
# layer is one line in this list and touches no other layer's list.

target_sources(pdfkit_core PRIVATE
  arithmetic_decoder.cpp
  ccitt_decoder.cpp
  jbig2_bitmap.cpp
  jbig2_decoder.cpp
  jbig2_huffman.cpp
  jbig2_regions.cpp
  jbig2_segments.cpp)
