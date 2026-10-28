# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# The ctypes binding to PDFium, the independent reader of the test tiers, and the
# child-process pool every caller goes through. PDFium on a hostile file can hang
# or crash, so no tool loads it in its own process: a Pool keeps worker processes
# (this file run with --serve), sends each a JSON request per file and kills and
# restarts a worker that does not answer within the time limit (30 s).
#
# Source: the function signatures are those of PDFium's public headers
# fpdfview.h, fpdf_text.h, fpdf_edit.h and fpdf_doc.h, read at
# https://pdfium.googlesource.com/pdfium/+/refs/heads/main/public/ ; the error
# codes (FPDF_ERR_*) and FPDF_PAGEOBJ_IMAGE = 3 and FPDF_ANNOT = 0x01 come from
# the same headers. FPDF_RenderPageBitmap takes (bitmap, page, start_x, start_y,
# size_x, size_y, rotate, flags) and its flag FPDF_ANNOT (0x01) is the only one
# that draws annotations; tests/pdfium/render.py passes flags 0 (read in
# fpdfview.h at https://raw.githubusercontent.com/chromium/pdfium/main/public/fpdfview.h).

from __future__ import annotations

import ctypes
import json
import os
import queue
import subprocess
import sys
import threading
from ctypes import (POINTER, c_char_p, c_double, c_float, c_int, c_ulong, c_ushort, c_void_p)
from pathlib import Path

ERRORS = {
    0: "success", 1: "unknown", 2: "file", 3: "format", 4: "password",
    5: "security", 6: "page",
}
FPDF_ANNOT = 0x01
FPDF_PAGEOBJ_IMAGE = 3
DEFAULT_TIMEOUT = 30.0
# The Info keys FPDF_GetMetaText answers (fpdf_doc.h), the metadata field list.
META_TAGS = ("Title", "Author", "Subject", "Keywords", "Creator", "Producer", "CreationDate", "ModDate")


def default_workers() -> int:
    """Half the logical processors, at least one: the comparisons leave the other half of the
    machine to its owner. Every tool's --workers takes another count."""
    return max(1, (os.cpu_count() or 2) // 2)


def default_dll() -> Path:
    # The sibling fetch tool knows the pinned location; it is loaded by path under
    # its own name because dataset/tools has a fetch module too.
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "pdfium_fetch", Path(__file__).resolve().parent / "fetch.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.dll_path()


class PdfiumError(Exception):
    def __init__(self, code: int):
        super().__init__(ERRORS.get(code, f"error {code}"))
        self.code = code
        self.name = ERRORS.get(code, f"error {code}")


class Library:
    """The in-process binding. Use it only inside a worker process."""

    def __init__(self, dll: Path):
        lib = ctypes.CDLL(str(dll))
        self.lib = lib
        sig = {
            "FPDF_InitLibrary": (None, []),
            "FPDF_DestroyLibrary": (None, []),
            "FPDF_LoadMemDocument": (c_void_p, [c_void_p, c_int, c_char_p]),
            "FPDF_GetLastError": (c_ulong, []),
            "FPDF_CloseDocument": (None, [c_void_p]),
            "FPDF_GetPageCount": (c_int, [c_void_p]),
            "FPDF_GetFileVersion": (c_int, [c_void_p, POINTER(c_int)]),
            "FPDF_GetSecurityHandlerRevision": (c_int, [c_void_p]),
            "FPDF_LoadPage": (c_void_p, [c_void_p, c_int]),
            "FPDF_ClosePage": (None, [c_void_p]),
            "FPDF_GetPageWidthF": (c_float, [c_void_p]),
            "FPDF_GetPageHeightF": (c_float, [c_void_p]),
            "FPDF_GetMetaText": (c_ulong, [c_void_p, c_char_p, c_void_p, c_ulong]),
            "FPDFText_LoadPage": (c_void_p, [c_void_p]),
            "FPDFText_ClosePage": (None, [c_void_p]),
            "FPDFText_CountChars": (c_int, [c_void_p]),
            "FPDFText_GetText": (c_int, [c_void_p, c_int, c_int, POINTER(c_ushort)]),
            "FPDFText_GetBoundedText": (c_int, [c_void_p, c_double, c_double, c_double, c_double,
                                                POINTER(c_ushort), c_int]),
            "FPDFBitmap_Create": (c_void_p, [c_int, c_int, c_int]),
            "FPDFBitmap_FillRect": (c_int, [c_void_p, c_int, c_int, c_int, c_int, c_ulong]),
            "FPDFBitmap_GetBuffer": (c_void_p, [c_void_p]),
            "FPDFBitmap_GetStride": (c_int, [c_void_p]),
            "FPDFBitmap_GetWidth": (c_int, [c_void_p]),
            "FPDFBitmap_GetHeight": (c_int, [c_void_p]),
            "FPDFBitmap_Destroy": (None, [c_void_p]),
            "FPDF_RenderPageBitmap": (None, [c_void_p, c_void_p, c_int, c_int, c_int, c_int, c_int, c_int]),
            "FPDFPage_CountObjects": (c_int, [c_void_p]),
            "FPDFPage_GetObject": (c_void_p, [c_void_p, c_int]),
            "FPDFPageObj_GetType": (c_int, [c_void_p]),
            "FPDFImageObj_GetBitmap": (c_void_p, [c_void_p]),
            "FPDFImageObj_GetImageDataDecoded": (c_ulong, [c_void_p, c_void_p, c_ulong]),
            "FPDFImageObj_GetImageDataRaw": (c_ulong, [c_void_p, c_void_p, c_ulong]),
        }
        for name, (restype, argtypes) in sig.items():
            fn = getattr(lib, name)
            fn.restype = restype
            fn.argtypes = argtypes
        lib.FPDF_InitLibrary()

    def close(self) -> None:
        self.lib.FPDF_DestroyLibrary()

    def reset(self) -> None:
        """Destroys and initialises the library again, with no document open, so the next
        answer depends on its own file only. PDFium keeps font state across documents: measured
        on the dataset, pdfminer-0006's page (a CIDFont+F1 it does not embed) renders other
        pixels after office-0001 (which embeds a font of that name) was rendered in the same
        process, and the same pixels again after a reset. A reset takes well under a
        millisecond."""
        self.lib.FPDF_DestroyLibrary()
        self.lib.FPDF_InitLibrary()

    def open(self, data: bytes, password: bytes | None = None) -> "Document":
        buf = ctypes.create_string_buffer(data, len(data))
        handle = self.lib.FPDF_LoadMemDocument(buf, len(data), password)
        if not handle:
            raise PdfiumError(int(self.lib.FPDF_GetLastError()))
        return Document(self, handle, buf)


class Document:
    def __init__(self, library: Library, handle, keepalive):
        self.library = library
        self.lib = library.lib
        self.handle = handle
        self._keepalive = keepalive

    def close(self) -> None:
        if self.handle:
            self.lib.FPDF_CloseDocument(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def page_count(self) -> int:
        return int(self.lib.FPDF_GetPageCount(self.handle))

    def version(self) -> int | None:
        value = c_int(0)
        return value.value if self.lib.FPDF_GetFileVersion(self.handle, ctypes.byref(value)) else None

    def security_revision(self) -> int:
        return int(self.lib.FPDF_GetSecurityHandlerRevision(self.handle))

    def meta(self, tag: str) -> str:
        key = tag.encode("ascii")
        size = self.lib.FPDF_GetMetaText(self.handle, key, None, 0)
        if size <= 2:
            return ""
        buf = ctypes.create_string_buffer(size)
        self.lib.FPDF_GetMetaText(self.handle, key, buf, size)
        return buf.raw[: size - 2].decode("utf-16-le", "replace")

    def page_text(self, index: int) -> str:
        page = self.lib.FPDF_LoadPage(self.handle, index)
        if not page:
            raise PdfiumError(6)
        try:
            text_page = self.lib.FPDFText_LoadPage(page)
            if not text_page:
                return ""
            try:
                count = self.lib.FPDFText_CountChars(text_page)
                if count <= 0:
                    return ""
                buf = (c_ushort * (count + 1))()
                written = self.lib.FPDFText_GetText(text_page, 0, count, buf)
                raw = bytes(memoryview(buf).cast("B"))[: max(written - 1, 0) * 2]
                return raw.decode("utf-16-le", "replace")
            finally:
                self.lib.FPDFText_ClosePage(text_page)
        finally:
            self.lib.FPDF_ClosePage(page)

    def bounded_text(self, index: int, box: list[float]) -> str:
        """The text of page `index` inside box [left, bottom, right, top] in page space
        (FPDFText_GetBoundedText)."""
        page = self.lib.FPDF_LoadPage(self.handle, index)
        if not page:
            raise PdfiumError(6)
        try:
            text_page = self.lib.FPDFText_LoadPage(page)
            if not text_page:
                return ""
            try:
                left, bottom, right, top = (float(v) for v in box)
                count = self.lib.FPDFText_GetBoundedText(text_page, left, top, right, bottom, None, 0)
                if count <= 0:
                    return ""
                buf = (c_ushort * (count + 1))()
                written = self.lib.FPDFText_GetBoundedText(text_page, left, top, right, bottom, buf, count + 1)
                raw = bytes(memoryview(buf).cast("B"))[: max(written, 0) * 2]
                return raw.decode("utf-16-le", "replace").rstrip("\x00")
            finally:
                self.lib.FPDFText_ClosePage(text_page)
        finally:
            self.lib.FPDF_ClosePage(page)

    def page_size(self, index: int) -> tuple[float, float]:
        page = self.lib.FPDF_LoadPage(self.handle, index)
        if not page:
            raise PdfiumError(6)
        try:
            return float(self.lib.FPDF_GetPageWidthF(page)), float(self.lib.FPDF_GetPageHeightF(page))
        finally:
            self.lib.FPDF_ClosePage(page)

    def render(self, index: int, dpi: float = 72.0, annotations: bool = False) -> dict:
        """Renders a page to BGRA on white; returns width, height, stride and bytes."""
        page = self.lib.FPDF_LoadPage(self.handle, index)
        if not page:
            raise PdfiumError(6)
        try:
            width = max(1, round(self.lib.FPDF_GetPageWidthF(page) * dpi / 72.0))
            height = max(1, round(self.lib.FPDF_GetPageHeightF(page) * dpi / 72.0))
            bitmap = self.lib.FPDFBitmap_Create(width, height, 1)
            if not bitmap:
                raise PdfiumError(1)
            try:
                self.lib.FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF)
                flags = FPDF_ANNOT if annotations else 0
                self.lib.FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, flags)
                stride = self.lib.FPDFBitmap_GetStride(bitmap)
                pointer = self.lib.FPDFBitmap_GetBuffer(bitmap)
                data = ctypes.string_at(pointer, stride * height)
                return {"width": width, "height": height, "stride": stride, "bgra": data}
            finally:
                self.lib.FPDFBitmap_Destroy(bitmap)
        finally:
            self.lib.FPDF_ClosePage(page)

    def page_images(self, index: int) -> list[dict]:
        """The image objects of a page with their raw and decoded data sizes."""
        page = self.lib.FPDF_LoadPage(self.handle, index)
        if not page:
            raise PdfiumError(6)
        try:
            out = []
            for i in range(self.lib.FPDFPage_CountObjects(page)):
                obj = self.lib.FPDFPage_GetObject(page, i)
                if self.lib.FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_IMAGE:
                    continue
                raw = self.lib.FPDFImageObj_GetImageDataRaw(obj, None, 0)
                decoded = self.lib.FPDFImageObj_GetImageDataDecoded(obj, None, 0)
                bitmap = self.lib.FPDFImageObj_GetBitmap(obj)
                size = None
                if bitmap:
                    size = [self.lib.FPDFBitmap_GetWidth(bitmap), self.lib.FPDFBitmap_GetHeight(bitmap)]
                    self.lib.FPDFBitmap_Destroy(bitmap)
                out.append({"object": i, "raw_bytes": int(raw), "decoded_bytes": int(decoded),
                            "bitmap": size})
            return out
        finally:
            self.lib.FPDF_ClosePage(page)


# ---------------------------------------------------------------------------
# The worker side


def handle_request(library: Library, request: dict) -> dict:
    """Answers one request: open a file and report what `ops` asks for."""
    path = request["path"]
    ops = set(request.get("ops", ["info"]))
    max_pages = int(request.get("max_pages", 100000))
    data = Path(path).read_bytes()
    password = request.get("password")
    try:
        doc = library.open(data, password.encode("utf-8") if password else None)
    except PdfiumError as err:
        return {"ok": False, "error": err.name, "code": err.code}
    with doc:
        result = {"ok": True, "pages": doc.page_count()}
        if "info" in ops:
            result["version"] = doc.version()
            result["security_revision"] = doc.security_revision()
            result["producer"] = doc.meta("Producer")
            result["creator"] = doc.meta("Creator")
        if "meta" in ops:
            result["meta"] = {tag: doc.meta(tag) for tag in META_TAGS}
        if "text" in ops:
            texts = []
            for index in range(min(result["pages"], max_pages)):
                try:
                    texts.append(doc.page_text(index))
                except PdfiumError:
                    texts.append(None)
            result["text"] = texts
        if "bounded" in ops:
            # Each [page index, left, bottom, right, top]: the text PDFium reads inside it.
            bounded = []
            for page_index, *box in request.get("bounded", []):
                try:
                    bounded.append(doc.bounded_text(int(page_index), box))
                except PdfiumError:
                    bounded.append(None)
            result["bounded"] = bounded
        if "images" in ops:
            result["images"] = [doc.page_images(i) for i in range(min(result["pages"], max_pages))]
        if "render" in ops:
            spec = request.get("render", {})
            out_dir = Path(spec["out"])
            out_dir.mkdir(parents=True, exist_ok=True)
            pages = spec.get("pages") or list(range(min(result["pages"], max_pages)))
            rendered = []
            for index in pages:
                image = doc.render(index, float(spec.get("dpi", 72.0)), bool(spec.get("annotations", False)))
                target = out_dir / f"page-{index + 1:04d}.bgra"
                header = f"{image['width']} {image['height']} {image['stride']}\n".encode("ascii")
                target.write_bytes(header + image["bgra"])
                rendered.append(str(target))
            result["rendered"] = rendered
        return result


def serve(dll: Path) -> None:
    """Reads one JSON request per line on stdin and answers one JSON line each."""
    library = Library(dll)
    stdin = sys.stdin.buffer
    stdout = sys.stdout
    for line in stdin:
        if not line.strip():
            continue
        try:
            request = json.loads(line)
            library.reset()
            response = handle_request(library, request)
        except Exception as err:  # a worker answers every request, whatever fails
            response = {"ok": False, "error": "worker", "detail": repr(err)}
        stdout.write(json.dumps(response, ensure_ascii=True) + "\n")
        stdout.flush()


# ---------------------------------------------------------------------------
# The caller side


class Worker:
    # The script a worker process runs with --serve; a subclass names its own.
    script = Path(__file__).resolve()

    def __init__(self, dll: Path):
        self.dll = dll
        self.proc = None
        self.lines: queue.Queue = queue.Queue()
        self.start()

    def start(self) -> None:
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        self.proc = subprocess.Popen(
            [sys.executable, "-I", str(self.script), "--serve", "--dll", str(self.dll)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            creationflags=flags,
        )
        self.lines = queue.Queue()
        proc, lines = self.proc, self.lines

        def pump():
            for raw in proc.stdout:
                lines.put(raw)
            lines.put(None)

        threading.Thread(target=pump, daemon=True).start()

    def kill(self) -> None:
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                pass

    def ask(self, request: dict, timeout: float) -> dict:
        if self.proc.poll() is not None:
            self.start()
        try:
            self.proc.stdin.write((json.dumps(request) + "\n").encode("utf-8"))
            self.proc.stdin.flush()
        except OSError:
            self.kill()
            self.start()
            return {"ok": False, "error": "crash", "detail": "the worker's input pipe closed"}
        try:
            raw = self.lines.get(timeout=timeout)
        except queue.Empty:
            self.kill()
            self.start()
            return {"ok": False, "error": "timeout", "detail": f"no answer within {timeout} s"}
        if raw is None:
            code = self.proc.poll()
            self.start()
            return {"ok": False, "error": "crash", "detail": f"the worker exited with {code}"}
        return json.loads(raw)


class Pool:
    """A fixed set of workers shared by threads; each call borrows one worker."""

    def __init__(self, size: int = 4, dll: Path | None = None, timeout: float = DEFAULT_TIMEOUT,
                 worker_class: type = Worker):
        self.dll = dll or default_dll()
        if not Path(self.dll).exists():
            raise FileNotFoundError(
                f"{self.dll} is missing; run `python tests/pdfium/fetch.py` to fetch PDFium")
        self.timeout = timeout
        self.free: queue.Queue = queue.Queue()
        self.workers = [worker_class(self.dll) for _ in range(size)]
        for worker in self.workers:
            self.free.put(worker)

    def ask(self, request: dict, timeout: float | None = None) -> dict:
        worker = self.free.get()
        try:
            return worker.ask(request, timeout or self.timeout)
        finally:
            self.free.put(worker)

    def probe(self, path: Path, ops=("info",), **extra) -> dict:
        request = {"path": str(path), "ops": list(ops)}
        request.update(extra)
        return self.ask(request)

    def close(self) -> None:
        for worker in self.workers:
            try:
                if worker.proc and worker.proc.poll() is None:
                    worker.proc.stdin.close()
                    worker.proc.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                worker.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description="PDFium binding and worker.")
    parser.add_argument("--serve", action="store_true", help="run as a worker on stdin/stdout")
    parser.add_argument("--dll", help="the pdfium.dll to load (default: the pinned cache)")
    parser.add_argument("files", nargs="*", help="files to open and describe")
    parser.add_argument("--text", action="store_true", help="also print each page's text")
    args = parser.parse_args()
    dll = Path(args.dll) if args.dll else default_dll()
    if args.serve:
        serve(dll)
        return 0
    with Pool(1, dll) as pool:
        for name in args.files:
            ops = ["info", "text"] if args.text else ["info"]
            print(json.dumps({"file": name, **pool.probe(Path(name), ops)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    sys.exit(main())
