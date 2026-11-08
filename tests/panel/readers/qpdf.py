# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# QPDF as a structure member of the reference panel, through the `qpdf`
# command. `structure`: `qpdf --show-npages <file>` (the page count; its
# failure, or no count printed, is QPDF's refusal of the file), then `qpdf
# --check <file>`: the PDF version, whether the file is encrypted or
# linearised, and the check's status from its exit (0 no errors, 3 warnings,
# 2 errors) with its first lines, or `crash` or `timeout` when the check fails
# so twice, the page count kept.
# `raw_streams`: for each object asked, the SHA-256 of `qpdf
# --show-object=<n> --raw-stream-data <file>`, the stream's bytes as stored
# (decrypted, not decoded), which is what the image census and PDFium's
# FPDFImageObj_GetImageDataRaw hash, so a member that names an image by its
# object number is aligned with one that names it by its bytes.
#
# Sources: `qpdf --help=--check`, `--help=--show-npages`, `--help=--show-object`
# and `--help=--raw-stream-data` of qpdf 11.9.0 (read on the Linux host on
# 2026-11-07); the exit statuses of --check (0, 2, 3) from `qpdf --help=exit-status`.

from __future__ import annotations

import hashlib
from pathlib import Path

import readers

NAME = "qpdf"
LICENCE = "Apache-2.0"
ROLE = "structure"
CAPABILITIES = ("structure", "raw_streams")
CHECK_STATUS = {0: "ok", 3: "warnings", 2: "errors"}


def version() -> str:
    return readers.pin(NAME)["version"]


def binary_digest() -> str:
    return readers.live_digest(readers.pin(NAME))


def missing() -> str | None:
    return readers.pin_problem(NAME)


def qpdf() -> str:
    return str(readers.binaries(readers.pin(NAME))[0][0])


def parse_check(exit_code: int, stdout: bytes, stderr: bytes) -> dict:
    lines = stdout.decode("utf-8", "replace").splitlines()
    result = {"check": CHECK_STATUS.get(exit_code, f"exit {exit_code}"), "version": None,
              "encrypted": None, "linearized": None}
    for line in lines:
        if line.startswith("PDF Version:"):
            result["version"] = line.split(":", 1)[1].strip()
        elif line.startswith("File is not encrypted"):
            result["encrypted"] = False
        elif line.startswith(("R = ", "P = ", "User password = ")):
            result["encrypted"] = True
        elif line.startswith("File is not linearized"):
            result["linearized"] = False
        elif line.startswith("File is linearized"):
            result["linearized"] = True
    if exit_code != 0:
        result["messages"] = readers.tail(stderr + b"\n" + stdout.split(b"\n", 4)[-1])
    return result


def answer(path: Path, kind: str, timeout: float, request: dict) -> dict:
    if kind == "structure":
        pages = readers.run([qpdf(), "--show-npages", str(path)], timeout)
        if readers.failed(pages):
            return pages
        if pages["exit"] not in (0, 3):
            return readers.refusal(pages["stderr"], pages["exit"])
        count = pages["stdout"].split()
        if not count or not count[0].isdigit():
            return readers.refusal(b"--show-npages printed no page count: " + pages["stderr"], pages["exit"])
        result = {"ok": True, "pages": int(count[0])}
        check = readers.run([qpdf(), "--check", str(path)], timeout)
        if check.get("error") == "repeated_failure":
            # The page count stands; the check that crashed or hung twice is reported as such.
            result.update(check=check["repeated"], messages=check.get("detail"))
            return result
        if readers.failed(check):
            return check
        result.update(parse_check(check["exit"], check["stdout"], check["stderr"]))
        return result
    if kind == "raw_streams":
        streams = {}
        for number in sorted({int(n) for n in request.get("objects", [])}):
            outcome = readers.run([qpdf(), f"--show-object={number}", "--raw-stream-data", str(path)],
                                  timeout)
            if readers.failed(outcome):
                return outcome
            if outcome["exit"] in (0, 3):
                streams[str(number)] = {"sha256": hashlib.sha256(outcome["stdout"]).hexdigest(),
                                        "bytes": len(outcome["stdout"])}
            else:
                streams[str(number)] = {"error": readers.tail(outcome["stderr"])}
        return {"ok": True, "streams": streams}
    return readers.unsupported(NAME, kind)
