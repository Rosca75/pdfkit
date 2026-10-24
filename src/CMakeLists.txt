# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The layers of CLAUDE.md section 3.1, in dependency order. Each folder adds its
# own sources to pdfkit_core.

add_subdirectory(platform)
add_subdirectory(syntax)
add_subdirectory(fonts)
add_subdirectory(text)
add_subdirectory(images)
add_subdirectory(writer)
add_subdirectory(redact)
add_subdirectory(api)
