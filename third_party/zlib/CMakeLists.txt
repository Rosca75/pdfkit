# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The zlib target: Flate decoding for the syntax layer and Flate encoding for
# the writer and the unit tier's fixture writer. Only the deflate and inflate
# sources are compiled; the gz file functions are left out, because the library
# opens no file. gzguts.h is kept because zutil.c includes it. zconf.h is the
# one shipped in the release archive, used as it is. The folder is a SYSTEM
# include directory, so the pinned warning set applies to our code and not to
# zlib's headers.

add_library(zlib STATIC
  adler32.c
  crc32.c
  deflate.c
  inffast.c
  inflate.c
  inftrees.c
  trees.c
  zutil.c)
target_include_directories(zlib SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
pdfkit_third_party_target(zlib zlib)
pdfkit_sanitize(zlib)
# zlib's objects are linked into the DLL, so they are compiled for its section
# garbage collection too.
target_compile_options(zlib PRIVATE
  "$<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>")
