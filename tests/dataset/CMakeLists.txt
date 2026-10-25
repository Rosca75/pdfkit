# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The dataset tier's C++ tools. pdfkit_font_census measures the fonts layer
# over the dataset (tests/dataset/font_census.py drives it); it links the
# static core, because the census is an internal measurement the C ABI does
# not offer.

add_executable(pdfkit_font_census font_census.cpp)
target_link_libraries(pdfkit_font_census PRIVATE pdfkit_core)
pdfkit_warnings(pdfkit_font_census)
pdfkit_sanitize(pdfkit_font_census)
pdfkit_static_runtime(pdfkit_font_census)
