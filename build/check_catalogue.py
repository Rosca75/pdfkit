# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Holds the function catalogue to one shape everywhere it appears (CLAUDE.md
# sections 2 and 3.6): the charter's table, the symbols the public header
# declares, the rows of the registry in src/api/catalogue.cpp, one file per
# function under src/api/, and one file per CLI verb under cli/verbs/ with its
# row in the verb table of cli/main.cpp. A row's build file must exist, at the
# root or in docs/archive/build/ once its phase is archived.
"""Check that the catalogue agrees everywhere: python build/check_catalogue.py [--root <tree>]."""

from __future__ import annotations

import re
from pathlib import Path

import checklib

# Every exported symbol outside the catalogue is named in the charter's
# lifecycle sentence, so no symbol is exempt here.
EXTRA_SYMBOLS: set[str] = set()

TABLE_ROW = re.compile(r"^\|\s*(\d+(?:\.\d+)?)\s*\|(.*)\|\s*$")
VERB_ROW = re.compile(r"^\|\s*`([a-z-]+)`\s*\|\s*([^|]*)\|")
LIFECYCLE = re.compile(r"lifecycle symbols carry no row of their own:(.*?)\.\s*$", re.S | re.M)
HEADER_SYMBOL = re.compile(r"\bPDFKIT_API\b[^;{]*?\b(pdfkit_\w+)\s*\(")
REGISTRY_ROW = re.compile(
    r'\{\s*"([\d.]+)"\s*,\s*"([^"]+)"\s*,\s*"(pdfkit_\w+)"\s*,\s*"([\w-]+)"\s*,\s*'
    r'"(build-\d+\.md)"\s*,\s*(?:Catalogue::Status::)?(\w+)\s*\}')
MAIN_VERB = re.compile(r'\{\s*"([a-z-]+)"\s*,\s*"')


def finding(path: str, line: int, failed: str, expected: str, fix: str) -> checklib.Finding:
    return checklib.Finding(path, line, failed, expected, fix)


def charter_section(text: str, heading: str) -> str:
    start = text.find(heading)
    if start < 0:
        return ""
    end = text.find("\n## ", start + len(heading))
    return text[start:] if end < 0 else text[start:end]


def parse_charter(text: str) -> tuple[list[dict[str, str]], list[str], dict[str, set[str]]]:
    section = charter_section(text, "## 2. The function catalogue")
    rows: list[dict[str, str]] = []
    verbs: dict[str, set[str]] = {}
    for line in section.splitlines():
        m = TABLE_ROW.match(line)
        if m:
            cells = [cell.strip() for cell in m.group(2).split("|")]
            symbol = re.search(r"`(pdfkit_\w+)`", cells[1])
            build = re.search(r"build-\d+\.md", cells[-1])
            rows.append({"number": m.group(1), "function": cells[0],
                         "symbol": symbol.group(1) if symbol else "",
                         "build_file": build.group(0) if build else ""})
            continue
        m = VERB_ROW.match(line)
        if m:
            verbs[m.group(1)] = {n.strip() for n in m.group(2).split(",") if n.strip()}
    lifecycle_match = LIFECYCLE.search(section)
    lifecycle = re.findall(r"`(pdfkit_\w+)`", lifecycle_match.group(1)) if lifecycle_match else []
    return rows, lifecycle, verbs


def build_file_exists(root: Path, name: str) -> bool:
    return (root / name).is_file() or (root / "docs" / "archive" / "build" / name).is_file()


def check(root: Path) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    charter = root / "CLAUDE.md"
    header = root / "include" / "pdfkit" / "pdfkit.h"
    registry = root / "src" / "api" / "catalogue.cpp"
    main_cpp = root / "cli" / "main.cpp"
    for path in (charter, header, registry, main_cpp):
        if not path.is_file():
            rel = path.relative_to(root).as_posix()
            findings.append(finding(rel, 0, f"{rel} is missing", "the catalogue's four sources",
                                    f"restore {rel}"))
    if findings:
        return findings

    rows, lifecycle, verbs = parse_charter(charter.read_text(encoding="utf-8"))
    if not rows or not verbs or not lifecycle:
        return [finding("CLAUDE.md", 0, "the catalogue, the verb table or the lifecycle sentence "
                        "of section 2 cannot be read", "the tables of CLAUDE.md section 2",
                        "keep the section's table layout: one '| <number> | ...' row per function")]
    symbols = [row["symbol"] for row in rows]

    declared = set(HEADER_SYMBOL.findall(header.read_text(encoding="utf-8")))
    wanted = set(symbols) | set(lifecycle) | EXTRA_SYMBOLS
    for symbol in sorted(wanted - declared):
        findings.append(finding("include/pdfkit/pdfkit.h", 0, f"{symbol} is not declared",
                                "every catalogue and lifecycle symbol of CLAUDE.md section 2",
                                f"declare PDFKIT_API ... {symbol}(...) in the header"))
    for symbol in sorted(declared - wanted):
        findings.append(finding("include/pdfkit/pdfkit.h", 0,
                                f"{symbol} is declared but has no row in CLAUDE.md section 2",
                                "a symbol per catalogue row or lifecycle symbol",
                                "add the function's row to CLAUDE.md section 2 first (section 3.6)"))

    registry_text = registry.read_text(encoding="utf-8")
    registry_rows = REGISTRY_ROW.findall(registry_text)
    if len(registry_rows) != len(rows):
        findings.append(finding("src/api/catalogue.cpp", 0,
                                f"the registry has {len(registry_rows)} rows, the charter "
                                f"{len(rows)}", "one registry row per row of CLAUDE.md section 2",
                                "add or remove registry rows so both tables match"))
    for row, entry in zip(rows, registry_rows):
        number, function, symbol, verb, build_file, status = entry
        line = checklib.line_of(registry_text, registry_text.find(f'"{symbol}"'))
        expected = (row["number"], row["function"].lower(), row["symbol"], row["build_file"])
        if (number, function.lower(), symbol, build_file) != expected:
            findings.append(finding("src/api/catalogue.cpp", line,
                                    f"the row {number} {symbol} {build_file} differs from the "
                                    f"charter's {row['number']} {row['symbol']} {row['build_file']}",
                                    "the number, function, symbol and first build file of the "
                                    "charter row, in the charter's order",
                                    "copy the values from CLAUDE.md section 2"))
        if verb not in verbs or number not in verbs[verb]:
            findings.append(finding("src/api/catalogue.cpp", line,
                                    f"the row {number} names the verb {verb}, whose Calls do not "
                                    f"include {number}",
                                    "a verb of CLAUDE.md section 2 that calls the function",
                                    "name the verb that exposes this function"))
        if status not in ("not_implemented", "implemented"):
            findings.append(finding("src/api/catalogue.cpp", line, f"the status {status} is unknown",
                                    "not_implemented or implemented", "use one of the two"))
        if not build_file_exists(root, build_file):
            findings.append(finding("src/api/catalogue.cpp", line,
                                    f"the build file {build_file} does not exist",
                                    f"{build_file} at the root or in docs/archive/build/",
                                    "correct the row's build file"))

    api_files = {p.rsplit("/", 1)[-1]: p for p in checklib.walk(root, ("src/api",))}
    for symbol in symbols:
        name = symbol.removeprefix("pdfkit_") + ".cpp"
        if name not in api_files:
            findings.append(finding(f"src/api/{name}", 0, f"{symbol} has no file src/api/{name}",
                                    "one file per catalogue function (CLAUDE.md section 3.6)",
                                    f"add src/api/{name} defining {symbol}"))
            continue
        text = checklib.read_text(root, api_files[name])
        if not re.search(rf'extern\s+"C"[^;{{]*\b{symbol}\s*\(', text):
            findings.append(finding(api_files[name], 0, f"the file does not define {symbol}",
                                    f'extern "C" ... {symbol}(...) {{ ... }}',
                                    "define the exported function in its own file"))
    api_text = "".join(checklib.read_text(root, p) for p in api_files.values() if p.endswith(".cpp"))
    for symbol in sorted(set(lifecycle) | EXTRA_SYMBOLS):
        if not re.search(rf'extern\s+"C"[^;{{]*\b{symbol}\s*\(', api_text):
            findings.append(finding("src/api/", 0, f"the lifecycle symbol {symbol} is not defined",
                                    f'an extern "C" definition of {symbol} under src/api/',
                                    "define it in src/api/lifecycle.cpp"))

    verb_files = {Path(p).stem for p in checklib.walk(root, ("cli/verbs",)) if p.endswith(".cpp")}
    verb_stems = {verb.replace("-", "_") for verb in verbs}
    for verb in sorted(verbs):
        if verb.replace("-", "_") not in verb_files:
            stem = verb.replace("-", "_")
            findings.append(finding(f"cli/verbs/{stem}.cpp", 0, f"the verb {verb} has no file",
                                    "one file per verb of CLAUDE.md section 2",
                                    f"add cli/verbs/{stem}.cpp"))
    for stem in sorted(verb_files):
        if stem not in verb_stems and not any(stem.startswith(v + "_") for v in verb_stems):
            findings.append(finding(f"cli/verbs/{stem}.cpp", 0,
                                    f"cli/verbs/{stem}.cpp is not a verb of CLAUDE.md section 2",
                                    "a verb file, or a helper named <verb>_<part>.cpp",
                                    "add the verb to the charter first, or rename the helper"))
    main_verbs = set(MAIN_VERB.findall(main_cpp.read_text(encoding="utf-8")))
    for verb in sorted(set(verbs) - main_verbs):
        findings.append(finding("cli/main.cpp", 0, f"the verb {verb} is missing from the verb table",
                                "every verb of CLAUDE.md section 2 dispatched by cli/main.cpp",
                                f"add {verb} to verb_table"))
    for verb in sorted(main_verbs - set(verbs)):
        findings.append(finding("cli/main.cpp", 0, f"the verb table has {verb}, which the charter "
                                "does not list", "the verbs of CLAUDE.md section 2",
                                "add the verb to the charter first"))
    return findings


def main() -> int:
    args = checklib.parse_args(__doc__)
    return checklib.report("check_catalogue", check(Path(args.root)))


if __name__ == "__main__":
    checklib.run_main(main)
