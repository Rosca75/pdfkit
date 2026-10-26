# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The compile cache: every C and C++ compile goes through ccache when it is
# present, so a second build of the same sources, in this tree or in another
# worktree, takes its objects from the cache instead of the compiler. ccache
# ships with the pinned WinLibs toolchain (build/toolchain.md); it is a
# developer tool, never linked or shipped, and a build without it compiles the
# same objects. Its settings travel on its command line (KEY=VALUE arguments,
# ccache's second invocation form), not in the environment, so no launcher
# wrapper and no server is needed:
#   cache_dir    build/cache/ccache of the main checkout (CLAUDE.md section 11),
#                one cache for every worktree and preset
#   base_dir     this source tree, so its absolute paths are rewritten relative
#                to the build folder and two worktrees hash the same command
#   hash_dir     false, so a debug object is shared across worktrees too; its
#                debug information then names the build folder that compiled it
#                first, while file and line stay right
#   depend_mode  true: a miss runs the compiler once and reads its -MD output
#                instead of running the preprocessor a second time
#   sloppiness   pch_defines,time_macros: the standard-library precompiled
#                header is hashed by its content, which ccache needs to cache
#                a compile that includes it
# PDFKIT_COMPILER_CACHE is AUTO (use ccache when found), ON (fail when it is
# not) or OFF. PDFKIT_COMPILER_CACHE_DIR moves the cache.

set(PDFKIT_COMPILER_CACHE "AUTO" CACHE STRING "Compile through ccache: AUTO, ON or OFF")
set_property(CACHE PDFKIT_COMPILER_CACHE PROPERTY STRINGS AUTO ON OFF)

function(pdfkit_main_checkout out)
  execute_process(
    COMMAND git rev-parse --path-format=absolute --git-common-dir
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE common_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE git_result
    ERROR_QUIET)
  if(git_result EQUAL 0)
    get_filename_component(main_checkout "${common_dir}" DIRECTORY)
  else()
    # A source tree outside git (an unpacked archive) keeps its cache beside it.
    set(main_checkout "${PROJECT_SOURCE_DIR}")
  endif()
  set(${out} "${main_checkout}" PARENT_SCOPE)
endfunction()

if(NOT PDFKIT_COMPILER_CACHE STREQUAL "OFF")
  find_program(PDFKIT_CCACHE ccache)
  if(NOT PDFKIT_CCACHE)
    if(PDFKIT_COMPILER_CACHE STREQUAL "ON")
      message(FATAL_ERROR "PDFKIT_COMPILER_CACHE is ON and ccache is not on PATH. Expected the "
        "ccache of the pinned WinLibs toolchain (build/toolchain.md); put its bin folder on PATH, "
        "or configure with -DPDFKIT_COMPILER_CACHE=OFF.")
    endif()
    message(STATUS "Compile cache: off (ccache not found)")
  else()
    if(NOT PDFKIT_COMPILER_CACHE_DIR)
      pdfkit_main_checkout(main_checkout)
      set(PDFKIT_COMPILER_CACHE_DIR "${main_checkout}/build/cache/ccache" CACHE PATH
        "The ccache folder every worktree shares")
    endif()
    set(launcher "${PDFKIT_CCACHE}"
      "cache_dir=${PDFKIT_COMPILER_CACHE_DIR}"
      "base_dir=${PROJECT_SOURCE_DIR}"
      "hash_dir=false"
      "depend_mode=true"
      "sloppiness=pch_defines,time_macros"
      "max_size=20G")
    set(CMAKE_C_COMPILER_LAUNCHER ${launcher})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${launcher})
    message(STATUS "Compile cache: ccache, ${PDFKIT_COMPILER_CACHE_DIR}")
  endif()
endif()
