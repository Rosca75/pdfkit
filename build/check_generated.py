# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Checks that every generated source is what its generator writes from the
# committed data: each generator of GENERATORS runs into a temporary folder and
# its output is compared byte for byte with the committed file, because a
# generated file edited by hand is data nobody can reproduce.
"""Check the generated sources: python build/check_generated.py [--root <tree>]."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

import checklib

# Each generator, with the files it writes under the tree it is given.
GENERATORS = [
    ("build/fonts/glyph_list.py", ["src/fonts/generated/glyph_list_table.cpp"]),
    ("build/fonts/core14.py", ["src/fonts/generated/core14_table.cpp"]),
    ("build/fonts/subset.py", ["src/writer/placeholder_font_data.inc"]),
    ("build/unicode/generate.py", ["src/platform/unicode/tables.inc"]),
]


def check(root: Path) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    with tempfile.TemporaryDirectory(prefix="pdfkit-generated-") as scratch:
        for generator, outputs in GENERATORS:
            script = root / generator
            if not script.is_file():
                findings.append(checklib.Finding(generator, 0, "the generator is missing",
                                                 "the generator of " + ", ".join(outputs),
                                                 "restore the generator"))
                continue
            done = subprocess.run([sys.executable, str(script), "--out", scratch], cwd=root,
                                  capture_output=True, text=True, check=False)
            if done.returncode != 0:
                findings.append(checklib.Finding(generator, 0, "the generator failed: "
                                                 + (done.stderr or done.stdout).strip()[-300:],
                                                 "a generator that runs over the committed data",
                                                 "fix the data or the generator"))
                continue
            for output in outputs:
                fresh = Path(scratch) / output
                committed = root / output
                if not committed.is_file():
                    findings.append(checklib.Finding(output, 0, "the generated file is missing",
                                                     f"the output of {generator}",
                                                     f"run python {generator}"))
                elif fresh.read_bytes() != committed.read_bytes():
                    findings.append(checklib.Finding(output, 0,
                                                     "the file differs from its generator's output",
                                                     f"the bytes python {generator} writes",
                                                     f"run python {generator} and commit the "
                                                     "result; never edit it by hand"))
    return findings


def main() -> int:
    args = checklib.parse_args(__doc__)
    return checklib.report("check_generated", check(Path(args.root)))


if __name__ == "__main__":
    checklib.run_main(main)
