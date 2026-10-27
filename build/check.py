# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The checks tier (CLAUDE.md section 8.1): runs every check of the repository
# in order and prints a summary of all of them. Three modes, from the cheapest:
#   --fast   (the pre-push hook) the checks that read files, clang-format, and
#            clang-tidy over the sources that reach a file changed since
#            origin/main, from the mingw-debug compile database (configured
#            when absent);
#   routine  (no flag) the same, after building mingw-debug with warnings as
#            errors;
#   --full   (the merge gate of a branch and every phase gate) a build of
#            mingw-debug, clang-tidy over every source with its cache set
#            aside, a build of mingw-release and the artefacts' import and
#            export tables.
# clang-tidy keeps each source's findings in a cache keyed by everything that
# decides them (build/check_format.py), so a routine run analyses only what
# changed; a source left out of a routine run was analysed by the full run that
# let its last change into main. The exit code is non-zero when any group fails.
"""Run every check: python build/check.py [--fast | --full] [--base <ref>]."""

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
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--fast", action="store_true",
                      help="no build: the file checks, formatting and tidy of the changed sources")
    mode.add_argument("--full", action="store_true",
                      help="the merge gate: both builds, tidy of every source, the artefact check")
    parser.add_argument("--base", default="origin/main",
                        help="the git reference a routine or fast run compares with (default origin/main)")
    args = parser.parse_args()
    results: list[tuple[str, bool, float]] = []
    for name, script in FILE_CHECKS:
        run(name, python(*script), results)
    database = ROOT / "out" / "mingw-debug" / "compile_commands.json"
    if args.fast:
        configured = database.is_file() or run("configure mingw-debug",
                                               ["cmake", "--preset", "mingw-debug"], results)
        if configured:
            run("formatting and tidy of the changed sources",
                python("check_format.py", "--changed-since", args.base), results)
        else:
            run("formatting", python("check_format.py", "--no-tidy"), results)
    else:
        built = run("configure mingw-debug", ["cmake", "--preset", "mingw-debug"], results)
        built = built and run("warnings (build mingw-debug)",
                              ["cmake", "--build", "--preset", "mingw-debug"], results)
        if built and args.full:
            run("formatting and tidy of every source", python("check_format.py", "--no-cache"), results)
        elif built:
            run("formatting and tidy of the changed sources",
                python("check_format.py", "--changed-since", args.base), results)
        else:
            run("formatting", python("check_format.py", "--no-tidy"), results)
    if args.full:
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
