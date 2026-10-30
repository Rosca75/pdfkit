# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Builds the release preset of one architecture and packs the artefact of
# CLAUDE.md section 10: dist/pdfkit-<version>-windows-<arch>.zip holding
# pdfkit.dll, its import library, pdfkit.h, pdfkit.exe and the three licence
# files. x64 builds with the pinned WinLibs GCC (mingw-release), arm64 with
# llvm-mingw's aarch64 clang (arm64-release); the entries are the same. The zip is deterministic
# (sorted entries, a fixed timestamp, fixed permissions), so one tree packs to
# one set of bytes. Each file and the zip are checked against build/budgets.tsv
# and the artefacts against build/check_artefact.py before the zip is written.
"""Build and package the release: python build/package.py [--arch x64|arm64] [--skip-build]."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The release preset of each architecture; the package is named for the architecture.
PRESETS = {"x64": "mingw-release", "arm64": "arm64-release"}
FIXED_TIME = (1980, 1, 1, 0, 0, 0)


def without_member_dates(data: bytes) -> bytes:
    """An ar archive (the import library) with every member's date set to 0.

    The linker writes the build's time into each member header of the import
    library it makes, so two builds of one tree differ there and nowhere else.
    A member header is 60 bytes after the 8-byte magic: name (16), date (12),
    owner (6), group (6), mode (8), size (10) and the two-byte end marker, and
    the member's data follows, padded to an even length (the System V ar
    format, recalled; two clean builds pack to one zip with it)."""
    if not data.startswith(b"!<arch>\n"):
        return data
    out = bytearray(data)
    at = 8
    while at + 60 <= len(out):
        header = out[at:at + 60]
        if header[58:60] != b"`\n":
            break
        out[at + 16:at + 28] = b"0".ljust(12)
        size = int(header[48:58].decode("ascii").strip() or "0")
        at += 60 + size + (size & 1)
    return bytes(out)


def project_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\(pdfkit\s+VERSION\s+([\d.]+)", text)
    if not m:
        raise SystemExit("package: CMakeLists.txt has no project(pdfkit VERSION x.y.z); expected the "
                         "version there; fix: restore the project() line")
    label = re.search(r'set\(PDFKIT_VERSION_LABEL\s+"([^"]*)"\)', text)
    if not label:
        raise SystemExit("package: CMakeLists.txt sets no PDFKIT_VERSION_LABEL; expected "
                         "set(PDFKIT_VERSION_LABEL \"...\") after project(); fix: restore that line")
    return f"{m.group(1)}-{label.group(1)}" if label.group(1) else m.group(1)


def budgets() -> dict[str, int]:
    rows: dict[str, int] = {}
    for line in (ROOT / "build" / "budgets.tsv").read_text(encoding="utf-8").splitlines()[1:]:
        if line.strip():
            name, limit = line.split("\t")[:2]
            rows[name] = int(limit)
    return rows


def over_budget(name: str, size: int, limits: dict[str, int], arch: str) -> str | None:
    """The budget breach of one artefact, or None. An arm64 artefact's row is named arm64/<name>,
    because the two compilers and runtimes make files of different sizes."""
    key = name if arch == "x64" else f"{arch}/{name}"
    limit = limits.get(key)
    if limit is not None and size > limit:
        return (f"package: {key} is {size} bytes, over its budget of {limit} bytes in "
                f"build/budgets.tsv; fix: shrink the artefact, or raise the budget with a reason")
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=sorted(PRESETS), default="x64",
                        help="the architecture to build and package (default x64)")
    parser.add_argument("--skip-build", action="store_true", help="package the preset's folder as is")
    parser.add_argument("--verify", action="store_true",
                        help="then rebuild the preset from clean, package again and fail unless the "
                             "two zips are byte-identical")
    args = parser.parse_args()
    preset = PRESETS[args.arch]
    if not args.skip_build:
        subprocess.run(["cmake", "--preset", preset], cwd=ROOT, check=True)
        subprocess.run(["cmake", "--build", "--preset", preset], cwd=ROOT, check=True)
    first = package(args.arch)
    if first is None:
        return 1
    if not args.verify:
        return 0
    # The rebuild compiles every source again: with the compile cache on, it would take the first
    # build's objects and prove only that linking is reproducible (build/compiler-cache.cmake).
    subprocess.run(["cmake", "--build", "--preset", preset, "--clean-first"], cwd=ROOT, check=True,
                   env=dict(os.environ, CCACHE_DISABLE="1"))
    second = package(args.arch)
    if second is None:
        return 1
    if first != second:
        print(f"package: two packagings differ (SHA-256 {first} then {second}); expected one set of "
              f"bytes per tree; fix: find what the build or the zip writes from time or path",
              file=sys.stderr)
        return 1
    print(f"package: two packagings, one rebuilt from clean, are byte-identical (SHA-256 {first})")
    return 0


def package(arch: str) -> str | None:
    """Checks and packs out/<preset> of one architecture; the zip's SHA-256, or None on a failure."""
    preset = PRESETS[arch]
    out = ROOT / "out" / preset
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
            problems.append(f"package: {path} is missing; expected it from the {preset} build or "
                            f"the repository; fix: build the preset first")
            continue
        problem = over_budget(Path(name).name, path.stat().st_size, limits, arch)
        if problem:
            problems.append(problem)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return None
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    target = dist / f"pdfkit-{project_version()}-windows-{arch}.zip"
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            data = files[name].read_bytes()
            if name.endswith(".lib"):
                data = without_member_dates(data)
            archive.writestr(info, data)
    problem = over_budget("zip", target.stat().st_size, limits, arch)
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
