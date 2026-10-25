# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The redaction-trap synthesiser (build-8.md section 2): one file per way a
# producer can draw a name so that a redactor misses part of it. Every file
# draws the marker "Zebulon Quarterfield", an invented name, which the dataset
# verb's seeded job always redacts (tests/dataset/outcome.md), so the gate
# proves on each trap that the marker leaves the output. The module holding
# the PATTERNS table is traps.py.
