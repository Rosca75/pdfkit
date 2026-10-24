// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// What this build of the library is: its version, the compiler that built it
// and the build type, as pdfkit_version reports them. The compiler is named
// here because only the platform layer reads compiler macros; nothing here
// depends on the time, so two builds of one tree report the same text.

#ifndef PDFKIT_PLATFORM_BUILD_INFO_HPP
#define PDFKIT_PLATFORM_BUILD_INFO_HPP

#include <string>
#include <string_view>

namespace pdfkit::platform {

[[nodiscard]] std::string_view library_version() noexcept;

// Such as "gcc 16.1.0, Release".
[[nodiscard]] std::string build_description();

}  // namespace pdfkit::platform

#endif  // PDFKIT_PLATFORM_BUILD_INFO_HPP
