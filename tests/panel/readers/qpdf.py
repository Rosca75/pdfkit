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
# `annotations`: what the file's annotations draw, from `qpdf --json-output=2
# --json-key=qpdf --json-key=pages --json-stream-data=none <file>`: every
# annotation on every page by subtype, and whether one draws text: a widget
# (a form field) or a FreeText annotation always may, any other when one of
# its normal appearance streams (`qpdf --show-object=<n>
# --filtered-stream-data`), or a form XObject that stream draws through its
# /Resources, shows a string between BT and ET. A link draws none. `copy()` writes the file without its annotations and its form: the
# same JSON, each page's /Annots and the catalog's /AcroForm left out, given
# back through `qpdf --deterministic-id --decrypt <file> --update-from-json`,
# so the copy's bytes depend on the input alone and a member's answers about
# it are cached like any other. The copy is QPDF's writing of the file, so it
# also carries QPDF's repair of a damaged one.
#
# Sources: `qpdf --help=--check`, `--help=--show-npages`, `--help=--show-object`
# and `--help=--raw-stream-data` of qpdf 11.9.0 (read on the Linux host on
# 2026-11-07); the exit statuses of --check (0, 2, 3) from `qpdf --help=exit-status`;
# `--json-output`, `--json-key`, `--json-stream-data`, `--update-from-json`,
# `--deterministic-id` and `--filtered-stream-data` of the same help, and the
# JSON version 2 layout (an "obj:<n> <g> R" key per object holding "value" or
# "stream", the "pages" list naming each page's object) seen on synth-0011 and
# pdfbox-0011 (read on 2026-11-08).

from __future__ import annotations

import hashlib
import json
import re
import tempfile
from pathlib import Path

import readers

NAME = "qpdf"
LICENCE = "Apache-2.0"
ROLE = "structure"
CAPABILITIES = ("structure", "raw_streams")
CHECK_STATUS = {0: "ok", 3: "warnings", 2: "errors"}
REF = re.compile(r"(\d+) (\d+) R")
TEXT_SUBTYPES = ("Widget", "FreeText")
# The appearance streams read per file; a file whose others draw text is found by these.
APPEARANCE_LIMIT = 64
BLOCK = re.compile(rb"BT\b(.*?)\bET\b", re.S)
SHOW = re.compile(rb"[)>]\s*(?:Tj|'|\")|\]\s*TJ")
JSON_COMMAND = ("--json-output=2", "--json-key=qpdf", "--json-key=pages", "--json-stream-data=none")


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


def _object(objects: dict, value):
    """An indirect value followed to its object's value (a stream's entry as it is)."""
    seen = 0
    while isinstance(value, str) and REF.fullmatch(value) and seen < 32:
        entry = objects.get(f"obj:{value}")
        if entry is None:
            return None
        value = entry.get("value", entry)
        seen += 1
    return value


def survey(doc: dict) -> dict:
    """The annotations of a QPDF JSON document: their count by subtype, those that draw text by
    their subtype (widgets and FreeText), the object numbers of the other annotations' normal
    appearance streams (to read), and whether the catalog holds a form."""
    objects = doc["qpdf"][1]
    subtypes: dict[str, int] = {}
    text: dict[str, int] = {}
    appearances: list[int] = []
    count = 0
    for page in doc.get("pages") or []:
        record = _object(objects, page.get("object"))
        annots = _object(objects, record.get("/Annots")) if isinstance(record, dict) else None
        for item in annots if isinstance(annots, list) else []:
            annotation = _object(objects, item)
            if not isinstance(annotation, dict):
                continue
            count += 1
            subtype = str(annotation.get("/Subtype", "/none")).lstrip("/")
            subtypes[subtype] = subtypes.get(subtype, 0) + 1
            if subtype in TEXT_SUBTYPES:
                text[subtype] = text.get(subtype, 0) + 1
                continue
            appearance = _object(objects, annotation.get("/AP"))
            normal = appearance.get("/N") if isinstance(appearance, dict) else None
            if isinstance(normal, str) and "stream" in (objects.get(f"obj:{normal}") or {}):
                refs = [normal]
            else:
                # A dictionary of appearance states, each a stream.
                states = _object(objects, normal)
                refs = list(states.values()) if isinstance(states, dict) else []
            for ref in refs:
                match = REF.fullmatch(ref) if isinstance(ref, str) else None
                if match and "stream" in (objects.get(f"obj:{ref}") or {}):
                    appearances.append(int(match.group(1)))
    root = _object(objects, ((objects.get("trailer") or {}).get("value") or {}).get("/Root"))
    return {"annotations": count, "subtypes": dict(sorted(subtypes.items())), "text": text,
            "appearances": sorted(set(appearances)), "form": isinstance(root, dict) and "/AcroForm" in root}


def forms_of(objects: dict, number: int) -> list[int]:
    """The form XObjects a stream's /Resources name, by object number."""
    entry = objects.get(f"obj:{number} 0 R") or {}
    resources = _object(objects, ((entry.get("stream") or {}).get("dict") or {}).get("/Resources"))
    xobjects = _object(objects, resources.get("/XObject")) if isinstance(resources, dict) else None
    out = []
    for ref in (xobjects or {}).values() if isinstance(xobjects, dict) else []:
        match = REF.fullmatch(ref) if isinstance(ref, str) else None
        form = (objects.get(f"obj:{ref}") or {}).get("stream") if match else None
        if form and (form.get("dict") or {}).get("/Subtype") == "/Form":
            out.append(int(match.group(1)))
    return sorted(set(out))


def shows_text(content: bytes) -> bool:
    """Whether a content stream shows a string between BT and ET (Tj, TJ, ' or \")."""
    return any(SHOW.search(block.group(1)) for block in BLOCK.finditer(content))


def strip_update(doc: dict) -> dict:
    """The QPDF JSON update that leaves out each page's /Annots and the catalog's /AcroForm."""
    objects = doc["qpdf"][1]
    changed = {}
    keys = [f"obj:{page.get('object')}" for page in doc.get("pages") or []]
    root = ((objects.get("trailer") or {}).get("value") or {}).get("/Root")
    if isinstance(root, str):
        keys.append(f"obj:{root}")
    for key in keys:
        value = (objects.get(key) or {}).get("value")
        if isinstance(value, dict) and ("/Annots" in value or "/AcroForm" in value):
            changed[key] = {"value": {k: v for k, v in value.items() if k not in ("/Annots", "/AcroForm")}}
    return {"qpdf": [doc["qpdf"][0], changed]}


def read_json(path: Path, timeout: float) -> dict:
    """QPDF's JSON of a file: {"doc": ...}, or a refusal or transient failure."""
    outcome = readers.run([qpdf(), *JSON_COMMAND, str(path), "-"], timeout)
    if readers.failed(outcome):
        return outcome
    if outcome["exit"] not in (0, 3):
        return readers.refusal(outcome["stderr"], outcome["exit"])
    try:
        return {"doc": json.loads(outcome["stdout"])}
    except ValueError as err:
        return readers.refusal(f"JSON that does not read: {err}".encode("utf-8"), outcome["exit"])


def copy(path: Path, target: Path, timeout: float) -> dict | None:
    """Writes the file without its annotations and its form to `target`; None when written, else
    QPDF's refusal or a transient failure."""
    read = read_json(path, timeout)
    if "doc" not in read:
        return read
    with tempfile.TemporaryDirectory(dir=readers.cache_dir()) as folder:
        update = Path(folder) / "update.json"
        update.write_text(json.dumps(strip_update(read["doc"])), encoding="utf-8")
        written = Path(folder) / "copy.pdf"
        outcome = readers.run([qpdf(), "--deterministic-id", "--decrypt", str(path),
                               f"--update-from-json={update}", str(written)], timeout)
        if readers.failed(outcome):
            return outcome
        if outcome["exit"] not in (0, 3) or not written.exists():
            return readers.refusal(outcome["stderr"], outcome["exit"])
        target.parent.mkdir(parents=True, exist_ok=True)
        written.replace(target)
    return None


def answer(path: Path, kind: str, timeout: float, request: dict) -> dict:
    if kind == "annotations":
        read = read_json(path, timeout)
        if "doc" not in read:
            return read
        result = survey(read["doc"])
        text = dict(result["text"])
        objects = read["doc"]["qpdf"][1]
        read_count = 0
        for root in result["appearances"]:
            # An appearance and the forms it draws, depth first, within the per-file limit.
            queue, seen = [root], set()
            while queue and read_count < APPEARANCE_LIMIT:
                number = queue.pop()
                if number in seen:
                    continue
                seen.add(number)
                read_count += 1
                outcome = readers.run([qpdf(), f"--show-object={number}", "--filtered-stream-data", str(path)],
                                      timeout)
                if readers.failed(outcome):
                    return outcome
                if outcome["exit"] in (0, 3) and shows_text(outcome["stdout"]):
                    text["appearance"] = text.get("appearance", 0) + 1
                    break
                queue.extend(forms_of(objects, number))
        return {"ok": True, "annotations": result["annotations"], "subtypes": result["subtypes"],
                "text": text, "form": result["form"], "appearances": len(result["appearances"]),
                "streams_read": read_count, "draws_text": bool(text)}
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
