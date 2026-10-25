# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The unit tier: pdfkit_tests links the static core and runs every test file
# below under one doctest runner, registered with ctest under the label unit so
# the unit and sanitize test presets select it.

add_executable(pdfkit_tests
  main.cpp
  ../../cli/sha256.cpp
  cli/sha256_test.cpp
  platform/budget_test.cpp
  platform/buffer_test.cpp
  platform/checked_test.cpp
  platform/crypto/aes_test.cpp
  platform/crypto/md5_test.cpp
  platform/crypto/rc4_test.cpp
  platform/crypto/sha2_test.cpp
  platform/error_test.cpp
  platform/json_reader_test.cpp
  platform/json_writer_test.cpp
  support/content_builder.cpp
  support/document_summary.cpp
  support/pdf_builder.cpp
  support/pdf_builder_channels.cpp
  support/pdf_encryptor.cpp
  syntax/content_lexer_test.cpp
  syntax/cross_reference_test.cpp
  syntax/document_test.cpp
  syntax/filters_test.cpp
  syntax/lexer_test.cpp
  syntax/object_streams_test.cpp
  syntax/parser_test.cpp
  syntax/repair_test.cpp
  syntax/security_handler_test.cpp
  syntax/stream_test.cpp
  writer/channel_table_test.cpp
  writer/output_scan.cpp
  writer/page_selector_test.cpp
  writer/reachability_test.cpp
  writer/serialiser_test.cpp
  writer/stream_copier_test.cpp
  writer/xref_stream_writer_test.cpp
  api/abi_test.cpp
  api/open_inspect_test.cpp
  fonts/support/font_builder.cpp
  fonts/support/font_document.cpp
  fonts/cff_font_test.cpp
  fonts/cmap_parser_test.cpp
  fonts/decoding_ladder_test.cpp
  fonts/encodings_test.cpp
  fonts/font_census_test.cpp
  fonts/font_data_test.cpp
  fonts/font_widths_test.cpp
  fonts/glyph_list_test.cpp
  fonts/true_type_tables_test.cpp
  fonts/type1_font_test.cpp)
# Fixtures are written by the support library, included as "support/...".
# The CLI's own SHA-256 is compiled in too, as "cli/sha256.hpp", to check it
# against the library's.
target_include_directories(pdfkit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
                                                "${PROJECT_SOURCE_DIR}")
target_link_libraries(pdfkit_tests PRIVATE pdfkit_core doctest)
pdfkit_warnings(pdfkit_tests)
pdfkit_sanitize(pdfkit_tests)
pdfkit_static_runtime(pdfkit_tests)

add_test(NAME unit COMMAND pdfkit_tests --no-version)
set_tests_properties(unit PROPERTIES LABELS unit)
if(PDFKIT_SANITIZER_RUNTIME_DIR)
  set_tests_properties(unit PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${PDFKIT_SANITIZER_RUNTIME_DIR}")
endif()
