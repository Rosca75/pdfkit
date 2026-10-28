# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the redact layer, added to pdfkit_core. A new file of this
# layer is one line in this list and touches no other layer's list.

target_sources(pdfkit_core PRIVATE
  content_syntax.cpp
  edit_plan.cpp
  image_remover.cpp
  image_treatment_applier.cpp
  image_treatment_plan.cpp
  marked_content_rewriter.cpp
  match_finder.cpp
  optional_content_remover.cpp
  placeholder_drawing.cpp
  placeholder_fitting.cpp
  self_check.cpp
  text_plan.cpp
  text_redactor.cpp
  text_surgery.cpp
  write_pipeline.cpp)
