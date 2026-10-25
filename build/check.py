# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The checks tier (CLAUDE.md section 8.1): runs every check of the repository
# in order and prints a summary of all of them. The fast mode, which the
# pre-push hook uses, runs the checks that read files only, clang-format
# included; the full mode also builds mingw-debug with warnings as errors,
# runs clang-tidy over its compile database, builds mingw-release and reads
# the artefacts' import and export tables. The exit code is non-zero when any
# group fails.
"""Run every check: python build/check.py [--fast]."""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"

FILE_CHECKS = [
    ("layout", ["check_layout.py"]),
    ("headers", ["check_headers.py"]),
    ("comments", ["check_comments.py"]),
    ("catalogue", ["check_catalogue.py"]),
    ("third party", ["check_third_party.py"]),
    ("no network", ["check_network.py"]),
    ("repository", ["check_repo.py"]),
    ("paths", ["check_paths.py"]),
    ("generated", ["check_generated.py"]),
]


def run(name: str, command: list[str], results: list[tuple[str, bool, float]]) -> bool:
    print(f"== {name}: {' '.join(command)}", flush=True)
    start = time.monotonic()
    ok = subprocess.run(command, cwd=ROOT, check=False).returncode == 0
    results.append((name, ok, time.monotonic() - start))
    return ok


def python(script: str, *args: str) -> list[str]:
    return [sys.executable, str(BUILD / script), *args]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fast", action="store_true",
                        help="skip the builds, clang-tidy and the artefact check")
    args = parser.parse_args()
    results: list[tuple[str, bool, float]] = []
    for name, script in FILE_CHECKS:
        run(name, python(*script), results)
    if args.fast:
        run("formatting", python("check_format.py", "--no-tidy"), results)
    else:
        built = run("configure mingw-debug", ["cmake", "--preset", "mingw-debug"], results)
        built = built and run("warnings (build mingw-debug)",
                              ["cmake", "--build", "--preset", "mingw-debug"], results)
        if built:
            run("formatting and tidy", python("check_format.py"), results)
        else:
            run("formatting", python("check_format.py", "--no-tidy"), results)
        released = run("configure mingw-release", ["cmake", "--preset", "mingw-release"], results)
        released = released and run("build mingw-release",
                                    ["cmake", "--build", "--preset", "mingw-release"], results)
        if released:
            run("artefact", python("check_artefact.py", "out/mingw-release"), results)
    print("\n== summary")
    for name, ok, seconds in results:
        print(f"  {'ok    ' if ok else 'FAILED'}  {name} ({seconds:.1f} s)")
    failed = [name for name, ok, _ in results if not ok]
    print(f"check: {len(results) - len(failed)} of {len(results)} groups passed"
          + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
