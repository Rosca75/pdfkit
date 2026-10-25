# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the syntax layer, added to pdfkit_core. A new file of this
# layer is one line in this list and touches no other layer's list.

target_sources(pdfkit_core PRIVATE
  content_lexer.cpp
  cross_reference.cpp
  dictionary.cpp
  document.cpp
  filters.cpp
  lexer.cpp
  limits.cpp
  object.cpp
  object_streams.cpp
  page_tree.cpp
  parser.cpp
  repair.cpp
  resources.cpp
  security_handler.cpp
  stream.cpp)
