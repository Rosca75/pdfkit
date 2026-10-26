// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The libjpeg-turbo compression of a treated DCT image inside a setjmp
// guard, the encoding twin of src/images/dct_guard.c and for the same reason:
// libjpeg reports a fatal error by calling the error manager's error_exit,
// which must not return (libjpeg-turbo's doc/libjpeg.txt, "Error handling",
// recalled), so it longjmps back here, and no longjmp may cross a frame that
// holds a C++ object or a resource. This frame holds only the compressor and
// the memory destination's buffer, and frees both on every path. The
// compressed bytes are handed to the C++ caller through a callback, which
// copies them before the buffer is freed. Every component is sampled at full
// resolution (no chroma subsampling), so a JPEG block is 8 by 8 pixels in
// every component and a painted box loses at most one block of its edge to
// the transform. Grey and RGB samples are written as JFIF (RGB through
// YCbCr), CMYK samples as CMYK with the Adobe marker libjpeg writes for that
// colour space (jpeg_set_colorspace, recalled), as they were read: unchanged
// and uninverted.

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"

// The callback the C++ caller supplies: receives the compressed bytes; returns
// 0 when it kept them.
typedef int (*pdfkit_jpeg_deliver)(void* context, const unsigned char* data, size_t size);

struct pdfkit_jpeg_error {
  struct jpeg_error_mgr manager;
  jmp_buf jump;
  unsigned char* buffer;
};

static void pdfkit_jpeg_error_exit(j_common_ptr info) {
  struct pdfkit_jpeg_error* error = (struct pdfkit_jpeg_error*)info->err;
  longjmp(error->jump, 1);
}

static void pdfkit_jpeg_output_message(j_common_ptr info) {
  (void)info;
}

// Returns 0 when compressed and delivered, 1 on a libjpeg error (message
// filled), 2 when the callback refused. samples holds height rows of width *
// components bytes; components is 1, 3 or 4.
int pdfkit_jpeg_encode(const unsigned char* samples, unsigned width, unsigned height,
                       unsigned components, int quality, pdfkit_jpeg_deliver deliver,
                       void* context, char* message, size_t message_size) {
  struct jpeg_compress_struct info;
  struct pdfkit_jpeg_error error;
  unsigned long size = 0;
  memset(&info, 0, sizeof(info));
  memset(&error, 0, sizeof(error));
  info.err = jpeg_std_error(&error.manager);
  error.manager.error_exit = pdfkit_jpeg_error_exit;
  error.manager.output_message = pdfkit_jpeg_output_message;
  if (setjmp(error.jump)) {
    if (message != NULL && message_size > 0) {
      char text[JMSG_LENGTH_MAX];
      error.manager.format_message((j_common_ptr)&info, text);
      strncpy(message, text, message_size - 1);
      message[message_size - 1] = '\0';
    }
    jpeg_destroy_compress(&info);
    free(error.buffer);
    return 1;
  }
  jpeg_create_compress(&info);
  jpeg_mem_dest(&info, &error.buffer, &size);
  info.image_width = width;
  info.image_height = height;
  info.input_components = (int)components;
  info.in_color_space = components == 1 ? JCS_GRAYSCALE : components == 3 ? JCS_RGB : JCS_CMYK;
  jpeg_set_defaults(&info);
  if (components == 4) {
    jpeg_set_colorspace(&info, JCS_CMYK);
  }
  jpeg_set_quality(&info, quality, TRUE);
  for (int i = 0; i < info.num_components; ++i) {
    info.comp_info[i].h_samp_factor = 1;
    info.comp_info[i].v_samp_factor = 1;
  }
  jpeg_start_compress(&info, TRUE);
  size_t stride = (size_t)width * components;
  while (info.next_scanline < info.image_height) {
    JSAMPROW row = (JSAMPROW)(samples + (size_t)info.next_scanline * stride);
    jpeg_write_scanlines(&info, &row, 1);
  }
  jpeg_finish_compress(&info);
  int status = deliver(context, error.buffer, (size_t)size) == 0 ? 0 : 2;
  jpeg_destroy_compress(&info);
  free(error.buffer);
  return status;
}
