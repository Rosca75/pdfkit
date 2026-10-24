# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The ABI tier: loads pdfkit.dll from .NET (tests/abi/dotnet, through P/Invoke)
# and from Python (ctypes), and checks that both read the expected ABI version
# and the version JSON. A failure names which caller could not load the
# library and why, so a broken export or a missing runtime DLL is found here
# before any consumer meets it.
"""Run the ABI tier: python tests/abi/run.py [--preset mingw-release] [--dll <path>]."""

from __future__ import annotations

import argparse
import ctypes
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_ABI = 1


class PdfkitBuffer(ctypes.Structure):
    # The pdfkit_buffer struct of include/pdfkit/pdfkit.h.
    _fields_ = [
        ("data", ctypes.POINTER(ctypes.c_uint8)),
        ("size", ctypes.c_size_t),
        ("reserved", ctypes.c_void_p),
    ]


def find_dll(preset: str | None, dll: str | None) -> Path:
    if dll:
        return Path(dll).resolve()
    presets = [preset] if preset else ["mingw-release", "mingw-debug"]
    for name in presets:
        candidate = ROOT / "out" / name / "pdfkit.dll"
        if candidate.is_file():
            return candidate
    raise SystemExit(
        f"abi: no pdfkit.dll under out/{' or out/'.join(presets)}; expected a built library; "
        f"fix: cmake --preset {presets[0]} && cmake --build --preset {presets[0]}"
    )


def run_python(dll: Path) -> bool:
    try:
        library = ctypes.CDLL(str(dll))
    except OSError as error:
        print(f"abi: python could not load {dll}: {error}", file=sys.stderr)
        return False
    library.pdfkit_abi_version.restype = ctypes.c_uint32
    library.pdfkit_abi_version.argtypes = []
    library.pdfkit_version.restype = ctypes.c_int
    library.pdfkit_version.argtypes = [ctypes.POINTER(PdfkitBuffer)]
    library.pdfkit_buffer_free.restype = None
    library.pdfkit_buffer_free.argtypes = [ctypes.POINTER(PdfkitBuffer)]

    abi = library.pdfkit_abi_version()
    buffer = PdfkitBuffer()
    status = library.pdfkit_version(ctypes.byref(buffer))
    text = ctypes.string_at(buffer.data, buffer.size).decode("utf-8")
    library.pdfkit_buffer_free(ctypes.byref(buffer))
    print(text)
    print(f"python: pdfkit_abi_version() = {abi}, pdfkit_version status = {status}")
    version = json.loads(text)
    ok = abi == EXPECTED_ABI and status == 0 and version.get("abi") == EXPECTED_ABI
    ok = ok and not buffer.data
    if not ok:
        print(
            f"abi: python expected ABI version {EXPECTED_ABI}, status 0 and a freed buffer",
            file=sys.stderr,
        )
    return ok


def run_dotnet(dll: Path) -> bool:
    dotnet = shutil.which("dotnet")
    if dotnet is None:
        print(
            "abi: dotnet is not on PATH; expected the .NET SDK of build/toolchain.md; "
            "fix: install the .NET 10 SDK for the user and reopen the shell",
            file=sys.stderr,
        )
        return False
    project = ROOT / "tests" / "abi" / "dotnet" / "AbiHello.csproj"
    build = subprocess.run(
        [dotnet, "build", str(project), "-c", "Release", "--nologo", "-v", "quiet"],
        capture_output=True,
        text=True,
        check=False,
    )
    if build.returncode != 0:
        print(build.stdout + build.stderr, file=sys.stderr)
        print("abi: dotnet could not build tests/abi/dotnet", file=sys.stderr)
        return False
    app = ROOT / "tests" / "abi" / "dotnet" / "bin" / "Release" / "net10.0" / "AbiHello.dll"
    result = subprocess.run(
        [dotnet, str(app), str(EXPECTED_ABI), str(dll)],
        capture_output=True,
        text=True,
        check=False,
    )
    print(result.stdout, end="")
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr, end="")
        print(f"abi: dotnet could not load or check {dll} (exit {result.returncode})", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", help="the build folder under out/ to load pdfkit.dll from")
    parser.add_argument("--dll", help="an explicit path to pdfkit.dll")
    args = parser.parse_args()
    dll = find_dll(args.preset, args.dll)
    print(f"abi: loading {dll}")
    results = {"dotnet": run_dotnet(dll), "python": run_python(dll)}
    for caller, ok in results.items():
        print(f"abi: {caller}: {'ok' if ok else 'FAILED'}")
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
