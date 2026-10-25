# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The pdfkit command-line tool. It links the pdfkit shared library through its
# import library only, never pdfkit_core, so every CLI run proves the C ABI.
# On Windows its manifest sets the UTF-8 code page for paths.

add_executable(pdfkit_cli
  main.cpp
  arguments.cpp
  json_text.cpp
  output.cpp
  sha256.cpp
  support.cpp
  verbs/channels.cpp
  verbs/check.cpp
  verbs/dataset.cpp
  verbs/images.cpp
  verbs/inspect.cpp
  verbs/metadata.cpp
  verbs/redact.cpp
  verbs/select_pages.cpp
  verbs/text.cpp)
if(WIN32)
  enable_language(RC)
  target_sources(pdfkit_cli PRIVATE pdfkit.rc)
endif()
set_target_properties(pdfkit_cli PROPERTIES OUTPUT_NAME pdfkit)
target_include_directories(pdfkit_cli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                              "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(pdfkit_cli PRIVATE pdfkit)
pdfkit_warnings(pdfkit_cli)
pdfkit_sanitize(pdfkit_cli)
pdfkit_static_runtime(pdfkit_cli)
