# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The libjpeg-turbo target: DCT decoding for the images layer, and the JPEG
# compressor the unit tier encodes its fixtures with and the image treatments
# re-encode with. The sources are the libjpeg API library of the 3.2.0 release
# exactly as the release's CMakeLists.txt lists them in JPEG_SOURCES (read),
# including the 12- and 16-bit wrappers the API dispatches to, with arithmetic
# coding on (its default). The SIMD extensions are off (WITH_SIMD unset), because
# they need NASM, which the pinned toolchain lacks (build/toolchain.md); the
# TurboJPEG API and the tools are not built. The three configuration headers
# are generated from the release's templates into the binary folder with the
# values the release's CMake computes for GCC and clang on 64-bit Windows: the
# libjpeg v6.2 API, no hidden visibility (PE has none), GCC's forced inline,
# __thread, an 8-byte size_t and no __builtin_ctzl (long is 4 bytes there).
# BUILD is a fixed string, so two builds produce the same objects.

set(VERSION "3.2.0")
set(LIBJPEG_TURBO_VERSION_NUMBER 3002000)
set(JPEG_LIB_VERSION 62)
set(COPYRIGHT_YEAR "1991-2026")
set(BUILD "pdfkit")
set(HIDDEN "")
set(INLINE "__inline__ __attribute__((always_inline))")
set(THREAD_LOCAL "__thread")
set(SIZE_T 8)
# PACKAGE_NAME in jconfigint.h; this directory's scope only.
set(CMAKE_PROJECT_NAME "libjpeg-turbo")
set(C_ARITH_CODING_SUPPORTED 1)
set(D_ARITH_CODING_SUPPORTED 1)
set(SIMD_ARCHITECTURE NONE)
set(LJT_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file(src/jconfig.h.in "${LJT_CONFIG_DIR}/jconfig.h")
configure_file(src/jconfigint.h.in "${LJT_CONFIG_DIR}/jconfigint.h")
configure_file(src/jversion.h.in "${LJT_CONFIG_DIR}/jversion.h")

set(LJT "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_library(libjpeg-turbo STATIC
  ${LJT}/jcapimin.c
  ${LJT}/wrapper/jcapistd-8.c ${LJT}/wrapper/jcapistd-12.c ${LJT}/wrapper/jcapistd-16.c
  ${LJT}/wrapper/jccoefct-8.c ${LJT}/wrapper/jccoefct-12.c
  ${LJT}/wrapper/jccolor-8.c ${LJT}/wrapper/jccolor-12.c ${LJT}/wrapper/jccolor-16.c
  ${LJT}/wrapper/jcdctmgr-8.c ${LJT}/wrapper/jcdctmgr-12.c
  ${LJT}/wrapper/jcdiffct-8.c ${LJT}/wrapper/jcdiffct-12.c ${LJT}/wrapper/jcdiffct-16.c
  ${LJT}/jchuff.c ${LJT}/jcicc.c ${LJT}/jcinit.c ${LJT}/jclhuff.c
  ${LJT}/wrapper/jclossls-8.c ${LJT}/wrapper/jclossls-12.c ${LJT}/wrapper/jclossls-16.c
  ${LJT}/wrapper/jcmainct-8.c ${LJT}/wrapper/jcmainct-12.c ${LJT}/wrapper/jcmainct-16.c
  ${LJT}/jcmarker.c ${LJT}/jcmaster.c ${LJT}/jcomapi.c ${LJT}/jcparam.c ${LJT}/jcphuff.c
  ${LJT}/wrapper/jcprepct-8.c ${LJT}/wrapper/jcprepct-12.c ${LJT}/wrapper/jcprepct-16.c
  ${LJT}/wrapper/jcsample-8.c ${LJT}/wrapper/jcsample-12.c ${LJT}/wrapper/jcsample-16.c
  ${LJT}/jctrans.c ${LJT}/jdapimin.c
  ${LJT}/wrapper/jdapistd-8.c ${LJT}/wrapper/jdapistd-12.c ${LJT}/wrapper/jdapistd-16.c
  ${LJT}/jdatadst.c ${LJT}/jdatasrc.c
  ${LJT}/wrapper/jdcoefct-8.c ${LJT}/wrapper/jdcoefct-12.c
  ${LJT}/wrapper/jdcolor-8.c ${LJT}/wrapper/jdcolor-12.c ${LJT}/wrapper/jdcolor-16.c
  ${LJT}/wrapper/jddctmgr-8.c ${LJT}/wrapper/jddctmgr-12.c
  ${LJT}/wrapper/jddiffct-8.c ${LJT}/wrapper/jddiffct-12.c ${LJT}/wrapper/jddiffct-16.c
  ${LJT}/jdhuff.c ${LJT}/jdicc.c ${LJT}/jdinput.c ${LJT}/jdlhuff.c
  ${LJT}/wrapper/jdlossls-8.c ${LJT}/wrapper/jdlossls-12.c ${LJT}/wrapper/jdlossls-16.c
  ${LJT}/wrapper/jdmainct-8.c ${LJT}/wrapper/jdmainct-12.c ${LJT}/wrapper/jdmainct-16.c
  ${LJT}/jdmarker.c ${LJT}/jdmaster.c
  ${LJT}/wrapper/jdmerge-8.c ${LJT}/wrapper/jdmerge-12.c
  ${LJT}/jdphuff.c
  ${LJT}/wrapper/jdpostct-8.c ${LJT}/wrapper/jdpostct-12.c ${LJT}/wrapper/jdpostct-16.c
  ${LJT}/wrapper/jdsample-8.c ${LJT}/wrapper/jdsample-12.c ${LJT}/wrapper/jdsample-16.c
  ${LJT}/jdtrans.c ${LJT}/jerror.c ${LJT}/jfdctflt.c
  ${LJT}/wrapper/jfdctfst-8.c ${LJT}/wrapper/jfdctfst-12.c
  ${LJT}/wrapper/jfdctint-8.c ${LJT}/wrapper/jfdctint-12.c
  ${LJT}/wrapper/jidctflt-8.c ${LJT}/wrapper/jidctflt-12.c
  ${LJT}/wrapper/jidctfst-8.c ${LJT}/wrapper/jidctfst-12.c
  ${LJT}/wrapper/jidctint-8.c ${LJT}/wrapper/jidctint-12.c
  ${LJT}/wrapper/jidctred-8.c ${LJT}/wrapper/jidctred-12.c
  ${LJT}/jmemmgr.c ${LJT}/jmemnobs.c ${LJT}/jpeg_nbits.c
  ${LJT}/wrapper/jquant1-8.c ${LJT}/wrapper/jquant1-12.c
  ${LJT}/wrapper/jquant2-8.c ${LJT}/wrapper/jquant2-12.c
  ${LJT}/wrapper/jutils-8.c ${LJT}/wrapper/jutils-12.c ${LJT}/wrapper/jutils-16.c
  ${LJT}/jaricom.c ${LJT}/jcarith.c ${LJT}/jdarith.c)
target_include_directories(libjpeg-turbo SYSTEM PUBLIC "${LJT}" "${LJT_CONFIG_DIR}")
pdfkit_third_party_target(libjpeg-turbo libjpeg-turbo)
pdfkit_sanitize(libjpeg-turbo)
# The objects are linked into the DLL, so they are compiled for its section
# garbage collection too.
target_compile_options(libjpeg-turbo PRIVATE
  "$<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>")
