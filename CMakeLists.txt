# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The root build of pdfkit. It defines the four targets every other part of the
# repository relies on: pdfkit_core (the static library every test links),
# pdfkit (the shared library with the C ABI), pdfkit_cli (the pdfkit executable,
# which links the shared library only) and pdfkit_tests (the unit tier).
# Each layer and each vendored component adds itself from its own folder, so a
# new source file is one line in its own layer's list.

cmake_minimum_required(VERSION 3.28)

project(pdfkit VERSION 0.2.0 LANGUAGES C CXX)

# The release label after the version: "preview" for a pre-release, empty for a
# release. pdfkit_version reports the version with it and the package is named
# with it, so a pre-release never claims the release's version.
set(PDFKIT_VERSION_LABEL "preview")
if(PDFKIT_VERSION_LABEL)
  set(PDFKIT_VERSION_FULL "${PROJECT_VERSION}-${PDFKIT_VERSION_LABEL}")
else()
  set(PDFKIT_VERSION_FULL "${PROJECT_VERSION}")
endif()

# C++20 without compiler extensions, because the library must build on every
# toolchain a consumer uses (CMake documentation of CMAKE_CXX_EXTENSIONS, recalled).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The sources use no C++20 modules, so the per-file dependency scan is skipped.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Every artefact lands at the top of the binary folder, so pdfkit.exe and the
# test executables find pdfkit.dll beside them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

include("${PROJECT_SOURCE_DIR}/build/warnings.cmake")
include("${PROJECT_SOURCE_DIR}/build/runtime.cmake")
include("${PROJECT_SOURCE_DIR}/build/compiler-cache.cmake")
include("${PROJECT_SOURCE_DIR}/build/linker.cmake")

enable_testing()

# The vendored components, one target each.
add_subdirectory(third_party/doctest)
add_subdirectory(third_party/zlib)
add_subdirectory(third_party/libjpeg-turbo)
add_subdirectory(third_party/openjpeg)

# The static core: every layer adds its sources to it from src/<layer>/.
add_library(pdfkit_core STATIC)
target_include_directories(pdfkit_core PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src")
# The core's objects are the shared library's objects, so they carry the export
# attribute; an executable that links the core exports the same symbols, which
# is harmless.
target_compile_definitions(pdfkit_core
  PUBLIC PDFKIT_BUILDING
         PDFKIT_EXPORT_HEADER="platform/export.hpp"
  PRIVATE PDFKIT_VERSION_STRING="${PDFKIT_VERSION_FULL}"
          PDFKIT_BUILD_TYPE="$<IF:$<CONFIG:>,unspecified,$<CONFIG>>")
# zlib is public so the unit tier's fixture writer compresses with the same
# library the reader inflates with.
target_link_libraries(pdfkit_core PUBLIC zlib)
# libjpeg-turbo and openjpeg are public so the unit tier encodes its JPEG and
# JPEG 2000 fixtures with the encoders of the libraries the images layer
# decodes with.
target_link_libraries(pdfkit_core PUBLIC libjpeg-turbo)
target_link_libraries(pdfkit_core PUBLIC openjpeg)
pdfkit_warnings(pdfkit_core)
pdfkit_sanitize(pdfkit_core)
# The core's objects are linked into the DLL, so they are compiled for its
# section garbage collection too.
target_compile_options(pdfkit_core PRIVATE
  "$<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>")

add_subdirectory(src)

# The shared library: the core's objects behind the C ABI, with the compiler
# runtime linked statically so the DLL imports only operating-system DLLs.
# Linking pdfkit_core as well brings its own link dependencies (the vendored
# codecs); no archive member is pulled twice, because the objects already
# define every symbol.
add_library(pdfkit SHARED "$<TARGET_OBJECTS:pdfkit_core>")
target_link_libraries(pdfkit PRIVATE pdfkit_core)
set_target_properties(pdfkit PROPERTIES
  PREFIX ""
  IMPORT_PREFIX ""
  IMPORT_SUFFIX ".lib"
  OUTPUT_NAME pdfkit)
pdfkit_warnings(pdfkit)
pdfkit_sanitize(pdfkit)
pdfkit_static_runtime(pdfkit)

add_subdirectory(cli)
add_subdirectory(tests/unit)
add_subdirectory(tests/dataset)
