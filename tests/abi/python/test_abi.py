# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The Python side of the ABI tier: the calls of tests/abi/dotnet through
# ctypes. Every exported symbol of include/pdfkit/pdfkit.h is declared with
# its argument and result types, and each catalogue function is called over
# the files of a case list (path, open expectation and id, tab-separated):
# every status is checked against the ones the case allows, a refusal's
# error object for its code, failed, expected and fix, every buffer for being
# zeroed by pdfkit_buffer_free, and a written output for opening again with
# the metadata written into it. The first failure names the file and the
# function and ends the run with a non-zero exit.
"""ABI tier from ctypes: python tests/abi/python/test_abi.py <expected-abi> <pdfkit.dll> <cases.tsv>."""

from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path

OK, INVALID_ARGUMENT, REFUSED, SELF_CHECK, NOT_IMPLEMENTED = 0, 1, 2, 3, 4
MARKER = "Zebulon Quarterfield"
TITLE = "ABI TITLE é"


class Buffer(ctypes.Structure):
    # The pdfkit_buffer struct of include/pdfkit/pdfkit.h.
    _fields_ = [("data", ctypes.POINTER(ctypes.c_uint8)), ("size", ctypes.c_size_t),
                ("reserved", ctypes.c_void_p)]


class AbiFailure(Exception):
    def __init__(self, file: str, function: str, message: str):
        super().__init__(f"{file}: {function}: {message}")


def declare(library: ctypes.CDLL) -> None:
    """Every exported symbol with its types, in the order of the header."""
    status, handle, text = ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p
    buffer = ctypes.POINTER(Buffer)
    signatures = {
        "pdfkit_abi_version": (ctypes.c_uint32, []),
        "pdfkit_version": (status, [buffer]),
        "pdfkit_buffer_free": (None, [buffer]),
        "pdfkit_close": (None, [handle]),
        "pdfkit_edit_new": (status, [handle, ctypes.POINTER(handle), buffer]),
        "pdfkit_edit_free": (None, [handle]),
        "pdfkit_open": (status, [ctypes.c_char_p, ctypes.c_size_t, text, ctypes.POINTER(handle), buffer]),
        "pdfkit_inspect": (status, [handle, text, buffer]),
        "pdfkit_text": (status, [handle, text, buffer]),
        "pdfkit_images": (status, [handle, text, buffer]),
        "pdfkit_image_data": (status, [handle, text, text, buffer]),
        "pdfkit_metadata": (status, [handle, text, buffer]),
        "pdfkit_channels": (status, [handle, text, buffer]),
        "pdfkit_find": (status, [handle, text, buffer]),
        "pdfkit_redact_text": (status, [handle, text, buffer]),
        "pdfkit_redact_images": (status, [handle, text, buffer]),
        "pdfkit_set_metadata": (status, [handle, text, buffer]),
        "pdfkit_select_pages": (status, [handle, text, buffer]),
        "pdfkit_write": (status, [handle, text, buffer, buffer]),
        "pdfkit_check": (status, [ctypes.c_char_p, ctypes.c_size_t, text, buffer]),
    }
    for name, (restype, argtypes) in signatures.items():
        function = getattr(library, name)
        function.restype = restype
        function.argtypes = argtypes


def utf8(text: str | None) -> bytes | None:
    return None if text is None else text.encode("utf-8")


class Session:
    def __init__(self, library: ctypes.CDLL):
        self.lib = library
        self.file = "(library)"
        self.implemented: set[str] = set()
        self.calls = 0

    def fail(self, function: str, message: str) -> None:
        raise AbiFailure(self.file, function, message)

    def take_bytes(self, function: str, buffer: Buffer) -> bytes:
        if not buffer.data and buffer.size:
            self.fail(function, "a buffer with a size and no data")
        data = ctypes.string_at(buffer.data, buffer.size) if buffer.size else b""
        self.lib.pdfkit_buffer_free(ctypes.byref(buffer))
        if buffer.data or buffer.size:
            self.fail(function, "pdfkit_buffer_free left the buffer set")
        return data

    def take(self, function: str, status: int, buffer: Buffer, *allowed: int) -> str:
        self.calls += 1
        text = self.take_bytes(function, buffer).decode("utf-8")
        if status not in allowed:
            self.fail(function, f"status {status}, expected one of {allowed}: {text[:300]}")
        if status != OK:
            self.check_error(function, text)
        return text

    def check_error(self, function: str, text: str) -> dict:
        error = self.json(function, text)
        for field in ("code", "failed", "expected", "fix"):
            if not isinstance(error.get(field), str) or not error[field]:
                self.fail(function, f"the error object has no {field}: {text[:300]}")
        return error

    def json(self, function: str, text: str):
        try:
            return json.loads(text)
        except ValueError as error:
            self.fail(function, f"the answer is not JSON ({error}): {text[:300]}")
            raise

    def expected(self, symbol: str) -> int:
        return OK if symbol in self.implemented else NOT_IMPLEMENTED

    def version(self, expected_abi: int) -> None:
        abi = self.lib.pdfkit_abi_version()
        if abi != expected_abi:
            self.fail("pdfkit_abi_version", f"returned {abi}, expected {expected_abi}")
        buffer = Buffer()
        status = self.lib.pdfkit_version(ctypes.byref(buffer))
        version = self.json("pdfkit_version", self.take("pdfkit_version", status, buffer, OK))
        if version["abi"] != expected_abi:
            self.fail("pdfkit_version", "abi differs from pdfkit_abi_version")
        self.implemented = {row["symbol"] for row in version["catalogue"] if row["implemented"]}
        print(f"python: abi {abi}, {len(self.implemented)} catalogue functions implemented")

    def open(self, data: bytes) -> tuple[int, int | None, str]:
        buffer, document = Buffer(), ctypes.c_void_p()
        status = self.lib.pdfkit_open(data, len(data), None, ctypes.byref(document), ctypes.byref(buffer))
        return status, document.value, self.take("pdfkit_open", status, buffer, OK, REFUSED)

    def refusals(self) -> None:
        self.file = "(refusals)"
        status, document, text = self.open(b"not a PDF at all")
        if status != REFUSED or self.json("pdfkit_open", text)["code"] != "not_pdf" or document:
            self.fail("pdfkit_open", f"junk bytes were not refused as not_pdf: {text}")
        buffer = Buffer()
        status = self.lib.pdfkit_inspect(None, None, ctypes.byref(buffer))
        self.take("pdfkit_inspect", status, buffer, INVALID_ARGUMENT)
        edit = ctypes.c_void_p()
        status = self.lib.pdfkit_edit_new(None, ctypes.byref(edit), ctypes.byref(buffer))
        self.take("pdfkit_edit_new", status, buffer, INVALID_ARGUMENT)
        if edit.value:
            self.fail("pdfkit_edit_new", "a null document gave an edit handle")
        self.lib.pdfkit_close(None)
        self.lib.pdfkit_edit_free(None)
        self.lib.pdfkit_buffer_free(ctypes.byref(buffer))

    def case(self, path: str, expect_open: str, file_id: str) -> None:
        self.file = file_id
        status, document, text = self.open(Path(path).read_bytes())
        if expect_open.startswith("refused:"):
            code = expect_open.split(":", 1)[1]
            if status != REFUSED or self.json("pdfkit_open", text)["code"] != code:
                self.fail("pdfkit_open", f"expected the refusal {code}, got status {status}")
            return
        if status != OK:
            self.fail("pdfkit_open", f"expected {expect_open}, got status {status}: {text}")
        try:
            self.read(document)
            self.edit(document)
        finally:
            self.lib.pdfkit_close(document)

    def call(self, function: str, *args, allowed=(OK,)) -> str:
        buffer = Buffer()
        status = getattr(self.lib, function)(*args, ctypes.byref(buffer))
        return self.take(function, status, buffer, *allowed)

    def read(self, document: int) -> None:
        inspected = self.json("pdfkit_inspect", self.call("pdfkit_inspect", document, None))
        pages = inspected["page_count"]
        self.call("pdfkit_text", document, b'{"pages": [1], "blocks": false}',
                  allowed=(self.expected("pdfkit_text"),))
        self.call("pdfkit_text", document, utf8(json.dumps({"pages": [pages + 1]})),
                  allowed=(INVALID_ARGUMENT, NOT_IMPLEMENTED))
        images = self.json("pdfkit_images", self.call("pdfkit_images", document, b'{"pages": [1]}'))
        for image in images["images"][:1]:
            buffer = Buffer()
            status = self.lib.pdfkit_image_data(document, utf8(image["identity"]), None, ctypes.byref(buffer))
            data = self.take_bytes("pdfkit_image_data", buffer)
            self.calls += 1
            if image["decodable"]:
                if status != OK or not (data.startswith(b"\x89PNG") or data.startswith(b"\xff\xd8")):
                    self.fail("pdfkit_image_data", f"{image['identity']} gave status {status}")
            else:
                self.check_error("pdfkit_image_data", data.decode("utf-8"))
        self.call("pdfkit_image_data", document, b"img:0000000000000000", None, allowed=(INVALID_ARGUMENT,))
        metadata = self.json("pdfkit_metadata", self.call("pdfkit_metadata", document, None))
        if len(metadata["fields"]) != 8:
            self.fail("pdfkit_metadata", "the answer does not hold the eight fields")
        channels = self.json("pdfkit_channels", self.call("pdfkit_channels", document, None))
        if not channels["channels"]:
            self.fail("pdfkit_channels", "no channel row")
        values = utf8(json.dumps({"values": [{"text": "e"}, {"text": MARKER}], "pages": [1], "context": 2}))
        found = self.call("pdfkit_find", document, values, allowed=(self.expected("pdfkit_find"),))
        if "pdfkit_find" in self.implemented:
            answer = self.json("pdfkit_find", found)
            if len(answer["fingerprint"]) != 64 or len(answer["values"]) != 2:
                self.fail("pdfkit_find", "the answer lacks the fingerprint or a value")
            for value in answer["values"]:
                if value["count"] != len(value["occurrences"]):
                    self.fail("pdfkit_find", "a count differs from its occurrences")
        self.call("pdfkit_find", document, b'{"values": [{"text": ""}]}',
                  allowed=(INVALID_ARGUMENT, NOT_IMPLEMENTED))

    def edit(self, document: int) -> None:
        edit, buffer = ctypes.c_void_p(), Buffer()
        status = self.lib.pdfkit_edit_new(document, ctypes.byref(edit), ctypes.byref(buffer))
        self.take("pdfkit_edit_new", status, buffer, OK)
        try:
            matches = {"matches": [{"search": MARKER, "placeholder": "[X]"}]}
            self.call("pdfkit_redact_text", edit, utf8(json.dumps(matches)),
                      allowed=(self.expected("pdfkit_redact_text"),))
            self.call("pdfkit_redact_text", edit, None, allowed=(INVALID_ARGUMENT,))
            self.call("pdfkit_redact_images", edit, b'{"treatments": []}',
                      allowed=(self.expected("pdfkit_redact_images"), INVALID_ARGUMENT))
            self.call("pdfkit_set_metadata", edit, utf8(json.dumps({"Title": TITLE})),
                      allowed=(self.expected("pdfkit_set_metadata"),))
            self.call("pdfkit_set_metadata", edit, b'{"Bogus": "x"}', allowed=(INVALID_ARGUMENT, NOT_IMPLEMENTED))
            self.call("pdfkit_select_pages", edit, b'{"pages": [1]}')
            self.call("pdfkit_select_pages", edit, b'{"pages": []}', allowed=(INVALID_ARGUMENT,))
            pdf = Buffer()
            status = self.lib.pdfkit_write(edit, None, ctypes.byref(pdf), ctypes.byref(buffer))
            output = self.take_bytes("pdfkit_write", pdf)
            self.take("pdfkit_write", status, buffer, OK, SELF_CHECK)
            if status == SELF_CHECK:
                if output:
                    self.fail("pdfkit_write", "a refused write handed bytes back")
                return
            self.reopen(output)
        finally:
            self.lib.pdfkit_edit_free(edit)

    def reopen(self, output: bytes) -> None:
        status, written, text = self.open(output)
        if status != OK:
            self.fail("pdfkit_open", f"the written output does not open again: {text}")
        try:
            metadata = self.json("pdfkit_metadata", self.call("pdfkit_metadata", written, None))
            if "pdfkit_set_metadata" in self.implemented and metadata["fields"][0]["info"] != TITLE:
                self.fail("pdfkit_metadata", "the written title does not read back")
            strings = utf8(json.dumps({"strings": [{"text": MARKER}]}))
            check = self.json("pdfkit_check", self.call("pdfkit_check", output, len(output), strings))
            if check["count"] != 0:
                self.fail("pdfkit_check", "the redacted marker survives in the output")
        finally:
            self.lib.pdfkit_close(written)


def main(argv: list[str]) -> int:
    if len(argv) != 4 or not argv[1].isdigit():
        print(__doc__, file=sys.stderr)
        return 2
    try:
        library = ctypes.CDLL(argv[2])
    except OSError as error:
        print(f"python: could not load {argv[2]}: {error}", file=sys.stderr)
        return 3
    declare(library)
    session = Session(library)
    files = 0
    try:
        session.version(int(argv[1]))
        session.refusals()
        for line in Path(argv[3]).read_text(encoding="utf-8").splitlines():
            cells = line.split("\t")
            if len(cells) == 3:
                session.case(*cells)
                files += 1
    except AbiFailure as failure:
        print(f"python: FAILED {failure}", file=sys.stderr)
        return 1
    print(f"python: {files} files, {session.calls} calls, every expectation held")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
