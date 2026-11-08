# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# lopdf as a structure member of the reference panel, through panel-lopdf
# (tests/panel/rust/src/bin/panel-lopdf.rs): whether lopdf loads the file, its
# page count, its object count, whether it is encrypted and decrypts with the
# empty user password, and the pages whose /MediaBox is reachable neither on
# the page nor through its /Parent chain.

from __future__ import annotations

from pathlib import Path

import readers
from readers import rust_member

NAME = "lopdf"
LICENCE = "MIT"
ROLE = "structure"
CAPABILITIES = ("structure",)


def version() -> str:
    return readers.pin(NAME)["version"]


def binary_digest() -> str:
    return readers.live_digest(readers.pin(NAME))


def missing() -> str | None:
    return readers.pin_problem(NAME)


def answer(path: Path, kind: str, timeout: float, request: dict) -> dict:
    del request
    if kind != "structure":
        return readers.unsupported(NAME, kind)
    return rust_member.run(NAME, path, timeout)
