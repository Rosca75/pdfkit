# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The linker of the presets that ship nothing. PDFKIT_LINKER is "default" (GNU
# ld of the pinned toolchain, which links every shipped artefact, so the release
# DLL is the bytes build/package.py --verify reproduces) or "lld": llvm-mingw's
# ld.lld, from the cached sanitizer toolchain (build/toolchain.md), driven by
# GCC with -fuse-ld=lld. Measured on the laptop, lld links the debug preset's
# DLL and executables a third faster, which is most of a one-file rebuild. GCC
# looks for ld.lld in the folder -B names, which also puts that folder on the
# library search path; it holds DLLs and no import library of a name the link
# asks for, and the artefacts' import tables are those of GNU ld. When the
# toolchain is absent the build links with GNU ld and says so.

set(PDFKIT_LINKER "default" CACHE STRING "Linker of GCC builds: default (GNU ld) or lld")
set_property(CACHE PDFKIT_LINKER PROPERTY STRINGS default lld)

if(PDFKIT_LINKER STREQUAL "lld" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  include("${CMAKE_CURRENT_LIST_DIR}/llvm-mingw-release.cmake")
  pdfkit_llvm_mingw_root(llvm_root "${PROJECT_SOURCE_DIR}")
  if(NOT llvm_root STREQUAL "" AND EXISTS "${llvm_root}/bin/ld.lld.exe")
    add_link_options("-B${llvm_root}/bin/" "-fuse-ld=lld")
    message(STATUS "Linker: lld, ${llvm_root}/bin/ld.lld.exe")
  else()
    message(STATUS "Linker: GNU ld (no ld.lld at ${llvm_root}/bin; fetch llvm-mingw as "
      "build/toolchain.md says to link with lld)")
  endif()
elseif(NOT PDFKIT_LINKER STREQUAL "default" AND NOT PDFKIT_LINKER STREQUAL "lld")
  message(FATAL_ERROR "PDFKIT_LINKER is '${PDFKIT_LINKER}'; expected default or lld.")
endif()
