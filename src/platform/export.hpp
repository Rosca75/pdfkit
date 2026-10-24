// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The pdfkit authors
//
// PDFKIT_API, the export attribute of every C ABI symbol. It is the one place
// the attribute is spelled, because only the platform layer holds compiler- and
// OS-specific code. The core's build names this file in PDFKIT_EXPORT_HEADER,
// so include/pdfkit/pdfkit.h includes it first and every declaration carries
// the attribute whatever order a source includes its headers in; a caller of
// the library leaves PDFKIT_API empty.

#ifndef PDFKIT_PLATFORM_EXPORT_HPP
#define PDFKIT_PLATFORM_EXPORT_HPP

#if defined(PDFKIT_BUILDING)
#if defined(_WIN32)
#define PDFKIT_API __declspec(dllexport)
#else
#define PDFKIT_API __attribute__((visibility("default")))
#endif
#else
#define PDFKIT_API
#endif

#endif  // PDFKIT_PLATFORM_EXPORT_HPP
