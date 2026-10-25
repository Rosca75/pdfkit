# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The planted-marker synthesiser: one specimen per row of the channel table
# (docs/DESIGN.md §15) and the page-selection leak fixture (§16). Every file
# plants a unique marker string in one channel only, so the writer's stripping
# is proven by searching each output for the marker. The modules holding a
# PATTERNS table are rows.py (the channel rows) and leak.py (the leak fixture);
# kit.py holds what they share.
