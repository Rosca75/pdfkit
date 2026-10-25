# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The dataset tier's C++ tools. Each links the static core, because what it
# measures is internal and the C ABI does not offer it.

# pdfkit_image_census measures the images layer's decoders over the dataset
# (tests/pdfium/image_agreement.py drives it and compares with PDFium).
add_executable(pdfkit_image_census image_census.cpp)
target_link_libraries(pdfkit_image_census PRIVATE pdfkit_core)
pdfkit_warnings(pdfkit_image_census)
pdfkit_sanitize(pdfkit_image_census)
pdfkit_static_runtime(pdfkit_image_census)
