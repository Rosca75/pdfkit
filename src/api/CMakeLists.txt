# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the api layer, added to pdfkit_core: the C ABI implementation,
# one file per catalogue function plus the registry, the handles, the argument
# checks, the catch-all and the lifecycle symbols.

target_sources(pdfkit_core PRIVATE
  arguments.cpp
  catalogue.cpp
  guarded.cpp
  handles.cpp
  lifecycle.cpp
  channels.cpp
  check.cpp
  image_data.cpp
  images.cpp
  inspect.cpp
  metadata.cpp
  open.cpp
  redact_images.cpp
  redact_text.cpp
  select_pages.cpp
  set_metadata.cpp
  text.cpp
  write.cpp)
