# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The doctest target: a header-only test framework, used by the unit tier only
# and never linked into the library. Its folder is a SYSTEM include directory,
# so the pinned warning set applies to our tests and not to doctest's header.

add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
