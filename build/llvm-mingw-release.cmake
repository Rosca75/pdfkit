# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The pinned llvm-mingw release and where it is unpacked, named once for the
# two files that use it: build/llvm-mingw.cmake (the clang-sanitize toolchain)
# and build/linker.cmake (lld for the presets that ship nothing). The release
# and its SHA-256 are recorded in build/toolchain.md. PDFKIT_LLVM_MINGW names
# another folder; otherwise it is the shared cache of the main checkout
# (CLAUDE.md section 11), found through git from `from`.

set(PDFKIT_LLVM_MINGW_RELEASE "llvm-mingw-20261020-ucrt-x86_64")

function(pdfkit_llvm_mingw_root out from)
  if(DEFINED ENV{PDFKIT_LLVM_MINGW})
    set(${out} "$ENV{PDFKIT_LLVM_MINGW}" PARENT_SCOPE)
    return()
  endif()
  execute_process(
    COMMAND git rev-parse --path-format=absolute --git-common-dir
    WORKING_DIRECTORY "${from}"
    OUTPUT_VARIABLE common_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE git_result
    ERROR_QUIET)
  if(NOT git_result EQUAL 0)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  get_filename_component(main_checkout "${common_dir}" DIRECTORY)
  set(${out} "${main_checkout}/build/cache/llvm-mingw/${PDFKIT_LLVM_MINGW_RELEASE}" PARENT_SCOPE)
endfunction()
