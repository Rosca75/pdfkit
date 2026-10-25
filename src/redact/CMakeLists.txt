# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the redact layer, added to pdfkit_core. A new file of this
# layer is one line in this list and touches no other layer's list.

target_sources(pdfkit_core PRIVATE
  edit_plan.cpp
  write_pipeline.cpp)
