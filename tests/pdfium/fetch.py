# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Fetches the PDFium binary the test tiers use as the independent reader:
# bblanchon's prebuilt pdfium-win-x64.tgz of one pinned release, downloaded into
# build/cache/pdfium/<tag>/ of the main checkout, checked against its pinned
# SHA-256, with bin/pdfium.dll and the licence files extracted. On a Linux host,
# where the test tiers run but nothing ships (build/toolchain.md, "Linux (test
# only)"), the same release's pdfium-linux-x64.tgz is fetched instead into
# build/cache/pdfium/<tag>-linux-x64/, with lib/libpdfium.so, so the two
# binaries never share a folder and the answers cache, keyed on the archive's
# digest (tests/pdfium/results_cache.py), never mixes their answers. PDFium is
# never linked into the library; this developer tool is a named exemption of the
# network check because it downloads and nothing it fetches ships.
#
# Sources: the release asset name, URL pattern and the Windows asset's digest
# were read on https://github.com/bblanchon/pdfium-binaries/releases (the GitHub
# releases API lists `pdfium-win-x64.tgz` with digest sha256:3452d110... for
# chromium/8111, published 2026-10-19). The Linux asset's digest was computed
# from its download, since the release page and the API were out of reach; the
# same download route served the Windows asset with its read digest.

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import sys
import tarfile
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "dataset" / "tools"))

import common  # noqa: E402

TAG = "chromium/8111"
LINUX = sys.platform.startswith("linux")
if LINUX:
    ASSET = "pdfium-linux-x64.tgz"
    SHA256 = "bdc60126399f39fe768566a18d4be055a7b3681bc9e248e8a6656b4f6cae0aa2"
    LIBRARY = "lib/libpdfium.so"
    FOLDER = TAG.replace("/", "-") + "-linux-x64"
else:
    ASSET = "pdfium-win-x64.tgz"
    SHA256 = "3452d11031050ec65e57f180215b57b9e6d465e3fbedafa8a77ad90885c7d23e"
    LIBRARY = "bin/pdfium.dll"
    FOLDER = TAG.replace("/", "-")
URL = f"https://github.com/bblanchon/pdfium-binaries/releases/download/{TAG}/{ASSET}"
MAX_BYTES = 64 * 1024 * 1024


def tag_dir() -> Path:
    return common.pdfium_cache_dir() / FOLDER


def dll_path() -> Path:
    """The PDFium library of this host: pdfium.dll, or libpdfium.so on Linux."""
    return tag_dir() / LIBRARY


def download() -> bytes:
    request = urllib.request.Request(URL, headers={"User-Agent": "pdfkit-pdfium-fetch"})
    with urllib.request.urlopen(request, timeout=120) as response:
        data = response.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise common.ToolError(
            f"{URL} is larger than {MAX_BYTES} bytes",
            "an archive of about 4 MB",
            "check the release on GitHub and update TAG and SHA256 together",
        )
    return data


def extract(data: bytes, into: Path) -> None:
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive.getmembers():
            name = member.name.lstrip("./")
            wanted = name == LIBRARY or name.upper().startswith("LICENSE") \
                or name.startswith("licenses/") or name == "VERSION"
            if not wanted or not member.isfile():
                continue
            target = (into / name).resolve()
            if into.resolve() not in target.parents:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is not None:
                target.write_bytes(source.read())


def fetch(force: bool = False) -> Path:
    """Returns the path of the PDFium library, downloading it when absent."""
    if dll_path().exists() and not force:
        return dll_path()
    archive = tag_dir() / ASSET
    if archive.exists():
        data = archive.read_bytes()
    else:
        try:
            data = download()
        except OSError as err:
            raise common.ToolError(
                f"downloading {URL} failed: {err}",
                "the GitHub release asset reachable over HTTPS",
                "check the proxy, or place the archive by hand at " + str(archive),
            ) from err
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        raise common.ToolError(
            f"{ASSET} of {TAG} has SHA-256 {digest}",
            f"the pinned SHA-256 {SHA256}",
            f"delete {archive} and fetch again; if the release changed, pin a new tag deliberately",
        )
    tag_dir().mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        archive.write_bytes(data)
    extract(data, tag_dir())
    if not dll_path().exists():
        raise common.ToolError(
            f"{ASSET} holds no {LIBRARY}",
            f"the bblanchon archive layout with {LIBRARY}",
            "check the archive layout and update extract()",
        )
    return dll_path()


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch the pinned PDFium binary.")
    parser.add_argument("--force", action="store_true", help="extract again even when present")
    args = parser.parse_args()
    path = fetch(args.force)
    print(json.dumps({"tag": TAG, "sha256": SHA256, "dll": str(path),
                      "bytes": os.path.getsize(path)}))
    return 0


if __name__ == "__main__":
    common.run_tool(main)
