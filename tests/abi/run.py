# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The ABI tier: loads pdfkit.dll from .NET (tests/abi/dotnet, every symbol
# declared with LibraryImport) and from Python (tests/abi/python/test_abi.py,
# ctypes) and calls every catalogue function over a case list: the smallest
# manifest file of each tag in CASE_TAGS, one file per refusal code the
# manifest expects, and one fixture written by the CLI (a redact job over the
# first of those files, with a text and a metadata section), so the C ABI is
# proven from both languages against what producers wrote and against what
# pdfkit itself writes. A caller that cannot load the library, a status other
# than the one a case allows, a malformed error object or a buffer left set
# fails the tier, naming the file and the function. Without the dataset the
# file cases are skipped by name with the command that supplies them, and the
# tier runs the library-level calls alone.
"""Run the ABI tier: python tests/abi/run.py [--preset mingw-release] [--dll <path>] [--cases N]."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "dataset" / "tools"))

import common  # noqa: E402

EXPECTED_ABI = 1
# One case per tag: the smallest manifest file that opens and carries it.
CASE_TAGS = (
    "gen_word", "gen_libreoffice", "gen_pdftex", "object_streams", "xref_stream", "incremental_update",
    "rc4_128", "aesv2", "aesv3_r6", "encrypt_metadata_false", "identity_cid", "type3", "vertical",
    "right_to_left", "img_dct", "img_jpx", "img_jbig2_globals", "img_ccitt_g4", "inline_image", "smask",
    "ch_annotation", "ch_acroform", "ch_xmp_history", "meta_utf16", "meta_both_disagree",
    "meta_xmp_custom_namespace", "meta_xmp_only", "redact_kerning",
)


def find_build(preset: str | None, dll: str | None) -> tuple[Path, Path | None]:
    """pdfkit.dll and, beside it, pdfkit.exe (None when absent)."""
    if dll:
        path = Path(dll).resolve()
        exe = path.with_name("pdfkit.exe")
        return path, exe if exe.is_file() else None
    presets = [preset] if preset else ["mingw-release", "mingw-debug"]
    for name in presets:
        candidate = ROOT / "out" / name / "pdfkit.dll"
        if candidate.is_file():
            exe = candidate.with_name("pdfkit.exe")
            return candidate, exe if exe.is_file() else None
    raise SystemExit(
        f"abi: no pdfkit.dll under out/{' or out/'.join(presets)}; expected a built library; "
        f"fix: cmake --preset {presets[0]} && cmake --build --preset {presets[0]}"
    )


def choose_cases(limit: int | None) -> list[tuple[str, str, str]]:
    """(path, open expectation, id) per case, the files that exist on disk."""
    rows = common.read_manifest()
    present = [r for r in rows if common.file_path(r).is_file()]
    if not present:
        print("abi: skipped the dataset cases: no manifest file is on disk; "
              "fix: python dataset/tools/fetch.py, then run the tier again")
        return []
    chosen: dict[str, dict] = {}
    by_size = sorted(present, key=lambda r: (int(r["bytes"] or 0), r["id"]))
    for tag in CASE_TAGS:
        for row in by_size:
            if row["expect_open"] in ("opens", "repaired") and tag in common.split_tags(row["tags"]):
                chosen.setdefault(row["id"], row)
                break
    for row in by_size:
        expect = row["expect_open"]
        if expect.startswith("refused:") and not any(r["expect_open"] == expect for r in chosen.values()):
            chosen[row["id"]] = row
    if not any(r["expect_open"] == "repaired" for r in chosen.values()):
        for row in by_size:
            if row["expect_open"] == "repaired":
                chosen[row["id"]] = row
                break
    cases = [(str(common.file_path(r)), r["expect_open"], r["id"]) for r in chosen.values()]
    cases.sort(key=lambda case: case[2])
    return cases[:limit] if limit else cases


def cli_fixture(exe: Path | None, cases: list[tuple[str, str, str]], folder: Path) -> list[tuple[str, str, str]]:
    """The output of a redact job the CLI writes over the first case that opens."""
    source = next((c for c in cases if not c[1].startswith("refused:")), None)
    if exe is None or source is None:
        print("abi: skipped the CLI fixture: "
              + ("no pdfkit.exe beside the library" if exe is None else "no dataset file opens"))
        return []
    output = folder / "cli-fixture.pdf"
    job = {"input": source[0], "output": str(output),
           "text": {"matches": [{"search": "Zebulon Quarterfield", "placeholder": "[REDACTED]"}]},
           "metadata": {"Title": "CLI FIXTURE"}}
    job_path = folder / "job.json"
    job_path.write_text(json.dumps(job), encoding="utf-8")
    result = subprocess.run([str(exe), "redact", str(job_path)], capture_output=True, text=True, check=False)
    if result.returncode != 0 or not output.is_file():
        print(result.stdout + result.stderr, file=sys.stderr)
        raise SystemExit(f"abi: the CLI could not write its fixture from {source[2]} (exit {result.returncode})")
    print(f"abi: the CLI wrote its fixture from {source[2]}")
    return [(str(output), "opens", "cli-fixture")]


def run_dotnet(dll: Path, cases_file: Path) -> bool:
    dotnet = shutil.which("dotnet")
    if dotnet is None:
        print("abi: dotnet is not on PATH; expected the .NET SDK of build/toolchain.md; "
              "fix: install the .NET 10 SDK for the user and reopen the shell", file=sys.stderr)
        return False
    project = ROOT / "tests" / "abi" / "dotnet" / "PdfkitAbi.csproj"
    build = subprocess.run([dotnet, "build", str(project), "-c", "Release", "--nologo", "-v", "quiet"],
                           capture_output=True, text=True, check=False)
    if build.returncode != 0:
        print(build.stdout + build.stderr, file=sys.stderr)
        print("abi: dotnet could not build tests/abi/dotnet", file=sys.stderr)
        return False
    app = ROOT / "tests" / "abi" / "dotnet" / "bin" / "Release" / "net10.0" / "PdfkitAbi.dll"
    result = subprocess.run([dotnet, str(app), str(EXPECTED_ABI), str(dll), str(cases_file)],
                            capture_output=True, text=True, encoding="utf-8", check=False)
    print(result.stdout, end="")
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr, end="")
        print(f"abi: dotnet failed against {dll} (exit {result.returncode})", file=sys.stderr)
        return False
    return True


def run_python(dll: Path, cases_file: Path) -> bool:
    script = ROOT / "tests" / "abi" / "python" / "test_abi.py"
    result = subprocess.run([sys.executable, "-I", str(script), str(EXPECTED_ABI), str(dll), str(cases_file)],
                            capture_output=True, text=True, encoding="utf-8", check=False)
    print(result.stdout, end="")
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr, end="")
        print(f"abi: python failed against {dll} (exit {result.returncode})", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", help="the build folder under out/ to load pdfkit.dll from")
    parser.add_argument("--dll", help="an explicit path to pdfkit.dll")
    parser.add_argument("--cases", type=int, help="at most this many dataset cases")
    args = parser.parse_args()
    dll, exe = find_build(args.preset, args.dll)
    print(f"abi: loading {dll}")
    with tempfile.TemporaryDirectory(prefix="pdfkit-abi-") as temporary:
        folder = Path(temporary)
        cases = choose_cases(args.cases)
        cases += cli_fixture(exe, cases, folder)
        cases_file = folder / "cases.tsv"
        cases_file.write_text("".join("\t".join(case) + "\n" for case in cases), encoding="utf-8")
        print(f"abi: {len(cases)} cases: {', '.join(case[2] for case in cases)}")
        results = {"dotnet": run_dotnet(dll, cases_file), "python": run_python(dll, cases_file)}
    for caller, ok in results.items():
        print(f"abi: {caller}: {'ok' if ok else 'FAILED'}")
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
