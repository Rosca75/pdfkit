# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The toolchain file of the clang-sanitize preset: llvm-mingw, used for the
# AddressSanitizer run of the unit tier only, because the pinned MinGW GCC ships
# no libasan (build/toolchain.md). The release, its SHA-256 and the command that
# fetches it are recorded in build/toolchain.md (the release is named in
# build/llvm-mingw-release.cmake). The toolchain is read from
# PDFKIT_LLVM_MINGW when set, else from the shared cache of the main checkout
# (CLAUDE.md section 11), so every worktree uses one copy.

include("${CMAKE_CURRENT_LIST_DIR}/llvm-mingw-release.cmake")

pdfkit_llvm_mingw_root(llvm_root "${CMAKE_CURRENT_LIST_DIR}")
if(llvm_root STREQUAL "")
  message(FATAL_ERROR "build/llvm-mingw.cmake could not run git to find the main "
    "checkout; expected a git checkout, or set PDFKIT_LLVM_MINGW to the llvm-mingw folder.")
endif()

if(NOT EXISTS "${llvm_root}/bin/clang++.exe")
  message(FATAL_ERROR "llvm-mingw is not at ${llvm_root}. Expected "
    "${PDFKIT_LLVM_MINGW_RELEASE} unpacked there; fetch it with the command in "
    "build/toolchain.md (section 'The sanitizer toolchain'), or set PDFKIT_LLVM_MINGW.")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER "${llvm_root}/bin/clang.exe")
set(CMAKE_CXX_COMPILER "${llvm_root}/bin/clang++.exe")
set(CMAKE_RC_COMPILER "${llvm_root}/bin/llvm-windres.exe")
# The tests find the sanitizer runtime DLLs through this folder.
set(PDFKIT_SANITIZER_RUNTIME_DIR "${llvm_root}/bin" CACHE PATH "Folder of the sanitizer runtime DLLs")
