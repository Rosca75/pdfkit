# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The unit tier: pdfkit_tests links the static core and runs every test file
# below under one doctest runner, registered with ctest under the label unit so
# the unit and sanitize test presets select it.

add_executable(pdfkit_tests
  main.cpp
  platform/budget_test.cpp
  platform/buffer_test.cpp
  platform/checked_test.cpp
  platform/error_test.cpp
  platform/json_reader_test.cpp
  platform/json_writer_test.cpp
  api/abi_test.cpp)
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
