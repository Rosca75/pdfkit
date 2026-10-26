# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the images layer, added to pdfkit_core. A new file of this
# layer is one line in this list and touches no other layer's list.
# dct_guard.c and treatments/jpeg_guard.c are the library's C sources: the
# setjmp guards around libjpeg-turbo's decompressor and compressor, which must
# hold no C++ object in their frames.

target_sources(pdfkit_core PRIVATE
  adobe_cmyk.cpp
  arithmetic_decoder.cpp
  ccitt_decoder.cpp
  colour_space.cpp
  dct_decoder.cpp
  dct_guard.c
  image_catalogue.cpp
  image_decoder.cpp
  image_extractor.cpp
  image_identity.cpp
  image_info.cpp
  image_listing.cpp
  jbig2_bitmap.cpp
  jbig2_decoder.cpp
  jbig2_huffman.cpp
  jbig2_regions.cpp
  jbig2_segments.cpp
  jpx_decoder.cpp
  masks.cpp
  pdf_function.cpp
  pixel_image.cpp
  png_writer.cpp
  sample_unpacker.cpp
  treatments/box_painter.cpp
  treatments/caption_drawer.cpp
  treatments/jpeg_guard.c
  treatments/mosaic_blur.cpp
  treatments/pixel_buffer.cpp
  treatments/reencoder.cpp
  treatments/region_mapper.cpp
  treatments/soft_mask_treatment.cpp)
