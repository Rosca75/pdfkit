// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The one C function of the library: the libjpeg-turbo decompression of a DCT
// image inside a setjmp guard. libjpeg reports a fatal error by calling the
// error manager's error_exit, which must not return (libjpeg-turbo's
// doc/libjpeg.txt, "Error handling", recalled); it longjmps back here. The
// rule this file exists for: no longjmp crosses a frame that holds a C++
// object or a resource, so the guard is C, its frame holds only the
// decompressor, and the decompressor is destroyed on both paths. The pixels go
// into a buffer the C++ caller reserves through a callback, after the header
// is read and before jpeg_start_decompress, so the caller checks the size
// against the decoding budget first. Components are kept as stored: grey,
// RGB (YCbCr converted by libjpeg) and CMYK (YCCK converted by libjpeg); a
// 12-bit JPEG is read through the 12-bit API and scaled to 8 bits.

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "jpeglib.h"

// The callback the C++ caller supplies: a buffer of width * height *
// components bytes, or NULL to stop (the caller keeps the reason).
typedef unsigned char* (*pdfkit_dct_reserve)(void* context, unsigned width, unsigned height,
                                             unsigned components);

struct pdfkit_dct_error {
  struct jpeg_error_mgr manager;
  jmp_buf jump;
  unsigned warnings;
};

static void pdfkit_dct_error_exit(j_common_ptr info) {
  struct pdfkit_dct_error* error = (struct pdfkit_dct_error*)info->err;
  longjmp(error->jump, 1);
}

static void pdfkit_dct_emit_message(j_common_ptr info, int level) {
  struct pdfkit_dct_error* error = (struct pdfkit_dct_error*)info->err;
  if (level < 0) {
    ++error->warnings;
  }
}

static void pdfkit_dct_output_message(j_common_ptr info) {
  (void)info;
}

// Returns 0 when decoded, 1 on a libjpeg error (message filled), 2 when the
// callback refused. color_transform is the /ColorTransform of /DecodeParms, or
// -1 when absent. adobe receives 1 when the stream holds an Adobe APP14
// marker.
int pdfkit_dct_decode(const unsigned char* data, size_t size, long max_memory, int color_transform,
                      pdfkit_dct_reserve reserve, void* context, int* adobe, unsigned* warnings,
                      char* message, size_t message_size) {
  struct jpeg_decompress_struct info;
  struct pdfkit_dct_error error;
  unsigned char* volatile out = NULL;
  memset(&info, 0, sizeof(info));
  memset(&error, 0, sizeof(error));
  info.err = jpeg_std_error(&error.manager);
  error.manager.error_exit = pdfkit_dct_error_exit;
  error.manager.emit_message = pdfkit_dct_emit_message;
  error.manager.output_message = pdfkit_dct_output_message;
  if (setjmp(error.jump)) {
    if (message != NULL && message_size > 0) {
      char text[JMSG_LENGTH_MAX];
      error.manager.format_message((j_common_ptr)&info, text);
      strncpy(message, text, message_size - 1);
      message[message_size - 1] = '\0';
    }
    *warnings = error.warnings;
    jpeg_destroy_decompress(&info);
    return 1;
  }
  jpeg_create_decompress(&info);
  info.mem->max_memory_to_use = max_memory;
  jpeg_mem_src(&info, data, (unsigned long)size);
  jpeg_read_header(&info, TRUE);
  *adobe = info.saw_Adobe_marker ? 1 : 0;
  unsigned components = (unsigned)info.num_components;
  if (components == 3) {
    if (color_transform == 0) {
      info.jpeg_color_space = JCS_RGB;
    } else if (color_transform == 1) {
      info.jpeg_color_space = JCS_YCbCr;
    }
    info.out_color_space = JCS_RGB;
  } else if (components == 4) {
    if (color_transform == 0) {
      info.jpeg_color_space = JCS_CMYK;
    } else if (color_transform == 1) {
      info.jpeg_color_space = JCS_YCCK;
    }
    info.out_color_space = JCS_CMYK;
  } else if (components == 1) {
    info.out_color_space = JCS_GRAYSCALE;
  } else {
    if (message != NULL && message_size > 0) {
      snprintf(message, message_size, "the JPEG has %u components", components);
    }
    *warnings = error.warnings;
    jpeg_destroy_decompress(&info);
    return 1;
  }
  if (info.data_precision != 8 && info.data_precision != 12) {
    if (message != NULL && message_size > 0) {
      snprintf(message, message_size, "the JPEG has a precision of %d bits", info.data_precision);
    }
    *warnings = error.warnings;
    jpeg_destroy_decompress(&info);
    return 1;
  }
  out = reserve(context, info.image_width, info.image_height, components);
  if (out == NULL) {
    *warnings = error.warnings;
    jpeg_destroy_decompress(&info);
    return 2;
  }
  jpeg_start_decompress(&info);
  size_t stride = (size_t)info.output_width * components;
  if (info.data_precision == 12) {
    J12SAMPROW row = (J12SAMPROW)(*info.mem->alloc_large)((j_common_ptr)&info, JPOOL_IMAGE,
                                                          stride * sizeof(J12SAMPLE));
    while (info.output_scanline < info.output_height) {
      size_t line = info.output_scanline;
      if (jpeg12_read_scanlines(&info, &row, 1) != 1) {
        break;
      }
      for (size_t i = 0; i < stride; ++i) {
        int value = row[i] >> 4;
        out[line * stride + i] = (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
      }
    }
  } else {
    while (info.output_scanline < info.output_height) {
      JSAMPROW row = out + (size_t)info.output_scanline * stride;
      if (jpeg_read_scanlines(&info, &row, 1) != 1) {
        break;
      }
    }
  }
  jpeg_finish_decompress(&info);
  *warnings = error.warnings;
  jpeg_destroy_decompress(&info);
  return 0;
}
