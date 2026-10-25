# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The sources of the writer layer, added to pdfkit_core. A new file of this
# layer is one line in this list and touches no other layer's list.

target_sources(pdfkit_core PRIVATE
  channel_table.cpp
  document_id.cpp
  file_writer.cpp
  object_graph.cpp
  page_selector.cpp
  reachability.cpp
  renumbering.cpp
  resource_usage.cpp
  serialiser.cpp
  stream_copier.cpp
  write_report.cpp
  xref_stream_writer.cpp)
