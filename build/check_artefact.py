# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Reads the import and export tables of a built pdfkit.dll and pdfkit.exe with
# objdump, GNU's from the pinned WinLibs toolchain for the x64 build or LLVM's
# from llvm-mingw for the ARM64 build, whose objdump on Windows is a shell
# script Python cannot start (CLAUDE.md section 10). Each may import only the operating-system
# DLLs listed below, because a P/Invoke or ctypes caller cannot be asked to
# ship a compiler runtime; the DLL must export every symbol the public header
# declares, undecorated, and nothing else, because an exported internal symbol
# is a surface someone will bind to.
"""Check the artefacts' imports and exports: python build/check_artefact.py [<build folder>]."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import checklib

# Each allowed import, with its reason.
ALLOWED_IMPORTS = {
    re.compile(r"^KERNEL32\.dll$", re.IGNORECASE): "the Windows core API, present on every system",
    re.compile(r"^api-ms-win-crt-[a-z]+-l1-1-0\.dll$", re.IGNORECASE):
        "the Universal C Runtime API sets, part of Windows 10 and later",
}
# The CLI loads the library it ships beside.
EXE_EXTRA = {re.compile(r"^pdfkit\.dll$", re.IGNORECASE): "the library the CLI calls"}
HEADER_SYMBOL = re.compile(r"\bPDFKIT_API\b[^;{]*?\b(pdfkit_\w+)\s*\(")


def objdump(path: Path) -> str:
    """The private headers of a PE file, read by PDFKIT_OBJDUMP when set, else by objdump, else
    by llvm-objdump."""
    tool = os.environ.get("PDFKIT_OBJDUMP") or shutil.which("objdump") or shutil.which("llvm-objdump")
    if tool is None:
        raise SystemExit("check_artefact: neither objdump nor llvm-objdump is on PATH; expected the "
                         "binutils of the pinned WinLibs toolchain or llvm-mingw's llvm-objdump "
                         "(build/toolchain.md); fix: put the toolchain's bin folder on PATH or set "
                         "PDFKIT_OBJDUMP")
    return subprocess.run([tool, "-p", str(path)], capture_output=True, text=True,
                          check=True).stdout


def imports_of(dump: str) -> list[str]:
    return re.findall(r"^\s*DLL Name:\s*(\S+)", dump, re.MULTILINE)


def exports_of(dump: str) -> list[str]:
    if "[Ordinal/Name Pointer] Table" not in dump:
        return llvm_exports_of(dump)
    section = dump.split("[Ordinal/Name Pointer] Table", 1)
    names: list[str] = []
    for line in section[1].splitlines()[1:]:
        m = re.match(r"^\s*\[\s*\d+\]\s*\+base\[\s*\d+\]\s+[0-9a-f]+\s+(\S+)", line)
        if not m:
            if names:
                break
            continue
        names.append(m.group(1))
    return names


def llvm_exports_of(dump: str) -> list[str]:
    """The export names of llvm-objdump's table: a line "Ordinal RVA Name" under "Export Table:",
    then one "<ordinal> 0x<rva> <name>" line per export."""
    section = dump.split("Export Table:", 1)
    if len(section) < 2:
        return []
    names: list[str] = []
    for line in section[1].splitlines():
        m = re.match(r"^\s*\d+\s+0x[0-9a-f]+\s+(\S+)\s*$", line)
        if m:
            names.append(m.group(1))
        elif names:
            break
    return names


def check(root: Path, folder: Path) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    header = root / "include" / "pdfkit" / "pdfkit.h"
    declared = set(HEADER_SYMBOL.findall(header.read_text(encoding="utf-8"))) \
        if header.is_file() else set()
    for name, extra in (("pdfkit.dll", {}), ("pdfkit.exe", EXE_EXTRA)):
        artefact = folder / name
        shown = artefact.as_posix()
        if not artefact.is_file():
            findings.append(checklib.Finding(shown, 0, f"{name} is missing",
                                             f"a built {name} in {folder.as_posix()}",
                                             "cmake --preset mingw-release && "
                                             "cmake --build --preset mingw-release"))
            continue
        dump = objdump(artefact)
        allowed = {**ALLOWED_IMPORTS, **extra}
        for imported in imports_of(dump):
            if not any(pattern.match(imported) for pattern in allowed):
                findings.append(checklib.Finding(
                    shown, 0, f"{name} imports {imported}",
                    "operating-system DLLs only: KERNEL32.dll and the api-ms-win-crt-* sets",
                    "link the compiler runtime statically (build/runtime.cmake); a new system "
                    "DLL is added to the list in build/check_artefact.py with its reason"))
        if name == "pdfkit.dll":
            exported = set(exports_of(dump))
            for symbol in sorted(declared - exported):
                findings.append(checklib.Finding(shown, 0, f"{symbol} is not exported undecorated",
                                                 "every symbol of include/pdfkit/pdfkit.h exported "
                                                 "by its C name",
                                                 "define it with extern \"C\" in src/api/ so the "
                                                 "header's PDFKIT_API applies"))
            for symbol in sorted(exported - declared):
                findings.append(checklib.Finding(shown, 0, f"{symbol} is exported",
                                                 "only the symbols the public header declares",
                                                 "remove the export attribute from the internal "
                                                 "symbol"))
    return findings


def main() -> int:
    args = checklib.parse_args(__doc__, [("folder", {"nargs": "?", "default": None})])
    root = Path(args.root)
    folder = Path(args.folder) if args.folder else root / "out" / "mingw-release"
    if not folder.is_absolute() and not folder.exists():
        folder = root / folder
    return checklib.report("check_artefact", check(root, folder))


if __name__ == "__main__":
    sys.exit(main())
