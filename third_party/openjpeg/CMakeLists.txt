# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The openjpeg target: JPEG 2000 decoding for the images layer. Only the
# openjp2 library sources of the OpenJPEG 2.5.4 release are compiled, as the
# release's own src/lib/openjp2/CMakeLists.txt lists them (read), without the
# JPIP index files, the codec tools and the executables. It is built without
# thread support (no MUTEX_ definition), so the decoder runs on the caller's
# thread and its output does not depend on scheduling. The two configuration
# headers are generated from the release's templates into the binary folder,
# with the values the release's CMake would set on this toolchain:
# _aligned_malloc and malloc.h exist under MinGW-w64 and the target is
# little-endian. OPJ_STATIC is public, because openjpeg.h marks its
# declarations dllimport on Windows otherwise.

set(OPENJPEG_VERSION_MAJOR 2)
set(OPENJPEG_VERSION_MINOR 5)
set(OPENJPEG_VERSION_BUILD 4)
set(PACKAGE_VERSION "2.5.4")
set(OPJ_HAVE_MALLOC_H 1)
set(OPJ_HAVE__ALIGNED_MALLOC 1)
set(OPJ_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file(src/lib/openjp2/opj_config.h.cmake.in "${OPJ_CONFIG_DIR}/opj_config.h" @ONLY)
configure_file(src/lib/openjp2/opj_config_private.h.cmake.in
  "${OPJ_CONFIG_DIR}/opj_config_private.h")

set(OPJ_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/openjp2")
add_library(openjpeg STATIC
  ${OPJ_SOURCE}/thread.c
  ${OPJ_SOURCE}/bio.c
  ${OPJ_SOURCE}/cio.c
  ${OPJ_SOURCE}/dwt.c
  ${OPJ_SOURCE}/event.c
  ${OPJ_SOURCE}/ht_dec.c
  ${OPJ_SOURCE}/image.c
  ${OPJ_SOURCE}/invert.c
  ${OPJ_SOURCE}/j2k.c
  ${OPJ_SOURCE}/jp2.c
  ${OPJ_SOURCE}/mct.c
  ${OPJ_SOURCE}/mqc.c
  ${OPJ_SOURCE}/openjpeg.c
  ${OPJ_SOURCE}/opj_clock.c
  ${OPJ_SOURCE}/pi.c
  ${OPJ_SOURCE}/t1.c
  ${OPJ_SOURCE}/t2.c
  ${OPJ_SOURCE}/tcd.c
  ${OPJ_SOURCE}/tgt.c
  ${OPJ_SOURCE}/function_list.c
  ${OPJ_SOURCE}/opj_malloc.c
  ${OPJ_SOURCE}/sparse_array.c)
target_include_directories(openjpeg SYSTEM PUBLIC "${OPJ_SOURCE}" "${OPJ_CONFIG_DIR}")
target_compile_definitions(openjpeg PUBLIC OPJ_STATIC)
pdfkit_third_party_target(openjpeg openjpeg)
pdfkit_sanitize(openjpeg)
# OpenJPEG's codec table (openjpeg.c) calls opj_jp2_destroy, opj_j2k_end_decompress and their kin
# through function pointers whose parameters are void * where the functions take their own codec
# types, which clang's function-type check (-fsanitize=function, part of the undefined set)
# reports on every JPEG 2000 image decoded. The calls are the library's design and the vendored
# sources are never edited, so that one check is off for this C target under the address and
# undefined set; every other check stays on, and pdfkit's own code keeps the function check.
if(PDFKIT_SANITIZE STREQUAL "address-undefined")
  target_compile_options(openjpeg PRIVATE -fno-sanitize=function)
endif()
# The objects are linked into the DLL, so they are compiled for its section
# garbage collection too.
target_compile_options(openjpeg PRIVATE
  "$<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>")
