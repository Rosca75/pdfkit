# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# What the three Rust members share: each is a binary of tests/panel/rust/,
# built by tests/panel/fetch.py into build/cache/panel/cargo-target/release/,
# that reads one PDF and writes its JSON answer into a file the adapter names
# (the crates print diagnostics of their own on standard output). Exit status 0
# is an answer, 2 the crate's refusal of the file with its message in the JSON;
# a binary killed by a signal (a stack overflow aborts the process) is a crash,
# asked once more and kept as a refusal only when it repeats (readers.run).

from __future__ import annotations

import json
from pathlib import Path

import readers


def binary(member: str) -> Path:
    return readers.binaries(readers.pin(member))[0][0]


def run(member: str, path: Path, timeout: float) -> dict:
    """The binary's JSON answer, its refusal, or a transient failure."""
    with readers.scratch() as folder:
        out = folder / "answer.json"
        outcome = readers.run([str(binary(member)), str(path), str(out)], timeout)
        if readers.failed(outcome):
            return outcome
        try:
            answer = json.loads(out.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {"ok": False, "error": "crash", "detail": f"exit {outcome['exit']} without an answer: "
                    + readers.tail(outcome["stderr"])}
    if not answer.get("ok"):
        return readers.refusal(answer.get("error", ""), outcome["exit"])
    return answer
