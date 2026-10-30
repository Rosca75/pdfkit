# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The toolchain file of the arm64 presets: llvm-mingw's clang for the
# aarch64-w64-mingw32 target, which builds the Windows ARM64 package. The
# pinned WinLibs GCC has no ARM64 target, and llvm-mingw keeps the gcc-
# compatible build (the same flags, the same static runtime through -static,
# the same import table promise), which is why it was chosen over MSVC. The
# release is the one build/llvm-mingw-release.cmake names, in its build for
# the host: llvm-mingw-<release>-ucrt-aarch64 on an ARM64 Windows machine (the
# release runner), the x86_64 or Linux build when cross-compiling. Its folder
# is read from PDFKIT_LLVM_MINGW_ARM64 when set, else from the shared cache of
# the main checkout (CLAUDE.md section 11); build/toolchain.md records the
# release, its SHA-256 and the fetch command.

include("${CMAKE_CURRENT_LIST_DIR}/llvm-mingw-release.cmake")

if(DEFINED ENV{PDFKIT_LLVM_MINGW_ARM64})
  set(llvm_root "$ENV{PDFKIT_LLVM_MINGW_ARM64}")
else()
  pdfkit_llvm_mingw_root(x64_root "${CMAKE_CURRENT_LIST_DIR}")
  if(x64_root STREQUAL "")
    message(FATAL_ERROR "build/llvm-mingw-arm64.cmake could not run git to find the main "
      "checkout; expected a git checkout, or set PDFKIT_LLVM_MINGW_ARM64 to the llvm-mingw folder.")
  endif()
  get_filename_component(cache "${x64_root}" DIRECTORY)
  set(llvm_root "${cache}/${PDFKIT_LLVM_MINGW_ARM64_RELEASE}")
endif()

# The prefixed driver names the target whatever the host, so a cross build and
# a native build call the same compiler.
set(suffix "")
if(CMAKE_HOST_WIN32)
  set(suffix ".exe")
endif()
set(driver "${llvm_root}/bin/aarch64-w64-mingw32-clang")
if(NOT EXISTS "${driver}${suffix}")
  message(FATAL_ERROR "llvm-mingw is not at ${llvm_root}. Expected "
    "${PDFKIT_LLVM_MINGW_ARM64_RELEASE} unpacked there; fetch it with the command in "
    "build/toolchain.md (section 'The ARM64 toolchain'), or set PDFKIT_LLVM_MINGW_ARM64.")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)
set(CMAKE_C_COMPILER "${driver}${suffix}")
set(CMAKE_CXX_COMPILER "${driver}++${suffix}")
set(CMAKE_RC_COMPILER "${llvm_root}/bin/aarch64-w64-mingw32-windres${suffix}")
