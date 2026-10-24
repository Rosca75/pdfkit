// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// The version comes from the project version in CMakeLists.txt and the build
// type from the CMake configuration, both passed as compile definitions; the
// compiler is read from its predefined macros.

#include "platform/build_info.hpp"

#ifndef PDFKIT_VERSION_STRING
#error "PDFKIT_VERSION_STRING is defined by CMakeLists.txt for pdfkit_core"
#endif
#ifndef PDFKIT_BUILD_TYPE
#error "PDFKIT_BUILD_TYPE is defined by CMakeLists.txt for pdfkit_core"
#endif

namespace pdfkit::platform {

namespace {

std::string compiler_name() {
#if defined(__clang__)
  return "clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." +
         std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
  return "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
         std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  return "msvc " + std::to_string(_MSC_VER);
#else
  return "unknown compiler";
#endif
}

}  // namespace

std::string_view library_version() noexcept {
  return PDFKIT_VERSION_STRING;
}

std::string build_description() {
  return compiler_name() + ", " + PDFKIT_BUILD_TYPE;
}

}  // namespace pdfkit::platform
