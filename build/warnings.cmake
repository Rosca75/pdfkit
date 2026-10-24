# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The pinned warning set and the sanitizer flags. Every target of ours calls
# pdfkit_warnings(); a warning is fixed, never silenced, because warnings are
# errors (CLAUDE.md section 9). Vendored C targets call
# pdfkit_third_party_target() instead, which turns warnings off for that
# component alone, because its code is not ours to change.
#
# Named suppressions: none. Each entry added here names its file, its line and
# the reason, one per line, in the list below, and is applied with
# set_source_files_properties on that file only.
set(PDFKIT_NAMED_SUPPRESSIONS "")

# The GCC warning options are those of the GCC manual's "Warning Options"
# chapter (recalled). -Wduplicated-cond and -Wlogical-op exist in GCC only, so
# the sanitizer toolchain (clang, build/toolchain.md) omits them.
set(PDFKIT_WARNINGS_COMMON
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wconversion
  -Wsign-conversion
  -Wshadow
  -Wold-style-cast
  -Wcast-align
  -Wnon-virtual-dtor
  -Woverloaded-virtual
  -Wnull-dereference
  -Wdouble-promotion
  -Wformat=2
  -Wimplicit-fallthrough
  -Wmisleading-indentation)
set(PDFKIT_WARNINGS_GCC_ONLY
  -Wduplicated-cond
  -Wlogical-op)

function(pdfkit_warnings target)
  set(flags ${PDFKIT_WARNINGS_COMMON})
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND flags ${PDFKIT_WARNINGS_GCC_ONLY})
  endif()
  # -Wold-style-cast, -Wnon-virtual-dtor and -Woverloaded-virtual are C++ only.
  target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${flags}>")
endfunction()

# component names the vendored component, so a reader of the build sees which
# code is compiled without warnings.
function(pdfkit_third_party_target target component)
  get_target_property(type ${target} TYPE)
  if(type STREQUAL "INTERFACE_LIBRARY")
    message(FATAL_ERROR "pdfkit_third_party_target(${target} ${component}): an interface "
      "target compiles nothing; give it a SYSTEM include directory instead.")
  endif()
  target_compile_options(${target} PRIVATE -w)
  set_property(TARGET ${target} PROPERTY PDFKIT_THIRD_PARTY_COMPONENT "${component}")
endfunction()

# PDFKIT_SANITIZE selects the sanitizer set of the sanitizer presets:
#   undefined-trap     UBSan in trap mode, which needs no runtime library and is
#                      what the pinned MinGW GCC supports (build/toolchain.md)
#   address-undefined  ASan and UBSan with their runtimes, under llvm-mingw
set(PDFKIT_SANITIZE "" CACHE STRING "Sanitizer set: empty, undefined-trap or address-undefined")
set_property(CACHE PDFKIT_SANITIZE PROPERTY STRINGS "" undefined-trap address-undefined)

function(pdfkit_sanitize target)
  if(PDFKIT_SANITIZE STREQUAL "")
    return()
  elseif(PDFKIT_SANITIZE STREQUAL "undefined-trap")
    set(flags -fsanitize=undefined -fsanitize-trap=all -fno-omit-frame-pointer)
  elseif(PDFKIT_SANITIZE STREQUAL "address-undefined")
    set(flags -fsanitize=address,undefined -fno-omit-frame-pointer)
  else()
    message(FATAL_ERROR "PDFKIT_SANITIZE is '${PDFKIT_SANITIZE}'; expected empty, "
      "undefined-trap or address-undefined. Use one of the sanitizer presets.")
  endif()
  target_compile_options(${target} PRIVATE ${flags})
  target_link_options(${target} PRIVATE ${flags})
endfunction()
