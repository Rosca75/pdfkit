# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Builds the release preset and packs the artefact of CLAUDE.md section 10:
# dist/pdfkit-<version>-windows-x64.zip holding pdfkit.dll, its import library,
# pdfkit.h, pdfkit.exe and the three licence files. The zip is deterministic
# (sorted entries, a fixed timestamp, fixed permissions), so one tree packs to
# one set of bytes. Each file and the zip are checked against build/budgets.tsv
# and the artefacts against build/check_artefact.py before the zip is written.
"""Build and package the release: python build/package.py [--skip-build]."""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRESET = "mingw-release"
FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def project_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\(pdfkit\s+VERSION\s+([\d.]+)", text)
    if not m:
        raise SystemExit("package: CMakeLists.txt has no project(pdfkit VERSION x.y.z); expected the "
                         "version there; fix: restore the project() line")
    return m.group(1)


def budgets() -> dict[str, int]:
    rows: dict[str, int] = {}
    for line in (ROOT / "build" / "budgets.tsv").read_text(encoding="utf-8").splitlines()[1:]:
        if line.strip():
            name, limit = line.split("\t")[:2]
            rows[name] = int(limit)
    return rows


def over_budget(name: str, size: int, limits: dict[str, int]) -> str | None:
    limit = limits.get(name)
    if limit is not None and size > limit:
        return (f"package: {name} is {size} bytes, over its budget of {limit} bytes in "
                f"build/budgets.tsv; fix: shrink the artefact, or raise the budget with a reason")
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true", help="package out/mingw-release as is")
    parser.add_argument("--verify", action="store_true",
                        help="then rebuild the preset from clean, package again and fail unless the "
                             "two zips are byte-identical")
    args = parser.parse_args()
    if not args.skip_build:
        subprocess.run(["cmake", "--preset", PRESET], cwd=ROOT, check=True)
        subprocess.run(["cmake", "--build", "--preset", PRESET], cwd=ROOT, check=True)
    first = package()
    if first is None:
        return 1
    if not args.verify:
        return 0
    subprocess.run(["cmake", "--build", "--preset", PRESET, "--clean-first"], cwd=ROOT, check=True)
    second = package()
    if second is None:
        return 1
    if first != second:
        print(f"package: two packagings differ (SHA-256 {first} then {second}); expected one set of "
              f"bytes per tree; fix: find what the build or the zip writes from time or path",
              file=sys.stderr)
        return 1
    print(f"package: two packagings, one rebuilt from clean, are byte-identical (SHA-256 {first})")
    return 0


def package() -> str | None:
    """Checks and packs out/<preset>; the zip's SHA-256, or None on a failure."""
    out = ROOT / "out" / PRESET
    check = subprocess.run([sys.executable, str(ROOT / "build" / "check_artefact.py"), str(out)],
                           cwd=ROOT, check=False)
    if check.returncode != 0:
        return None
    files = {
        "pdfkit.dll": out / "pdfkit.dll",
        "pdfkit.lib": out / "pdfkit.lib",
        "pdfkit.exe": out / "pdfkit.exe",
        "include/pdfkit/pdfkit.h": ROOT / "include" / "pdfkit" / "pdfkit.h",
        "LICENSE": ROOT / "LICENSE",
        "NOTICE": ROOT / "NOTICE",
        "THIRD-PARTY-NOTICES.md": ROOT / "THIRD-PARTY-NOTICES.md",
    }
    limits = budgets()
    problems = []
    for name, path in files.items():
        if not path.is_file():
            problems.append(f"package: {path} is missing; expected it from the {PRESET} build or "
                            f"the repository; fix: build the preset first")
            continue
        problem = over_budget(Path(name).name, path.stat().st_size, limits)
        if problem:
            problems.append(problem)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    target = dist / f"pdfkit-{project_version()}-windows-x64.zip"
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, files[name].read_bytes())
    problem = over_budget("zip", target.stat().st_size, limits)
    if problem:
        print(problem, file=sys.stderr)
        return None
    print(f"package: wrote {target.relative_to(ROOT).as_posix()} ({target.stat().st_size} bytes)")
    for name in sorted(files):
        print(f"  {name}: {files[name].stat().st_size} bytes")
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    print(f"  sha256: {digest}")
    return digest


if __name__ == "__main__":
    sys.exit(main())
