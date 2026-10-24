# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Fetches the PDFium binary the test tiers use as the independent reader:
# bblanchon's prebuilt pdfium-win-x64.tgz of one pinned release, downloaded into
# build/cache/pdfium/<tag>/ of the main checkout, checked against its pinned
# SHA-256, with bin/pdfium.dll and the licence files extracted. PDFium is never
# linked into the library; this developer tool is a named exemption of the
# network check because it downloads and nothing it fetches ships.
#
# Sources: the release asset name, URL pattern and the asset digest were read on
# https://github.com/bblanchon/pdfium-binaries/releases (the GitHub releases API
# lists `pdfium-win-x64.tgz` with digest sha256:3452d110... for chromium/8111,
# published 2026-10-19).

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
ASSET = "pdfium-win-x64.tgz"
URL = f"https://github.com/bblanchon/pdfium-binaries/releases/download/{TAG}/{ASSET}"
SHA256 = "3452d11031050ec65e57f180215b57b9e6d465e3fbedafa8a77ad90885c7d23e"
MAX_BYTES = 64 * 1024 * 1024


def tag_dir() -> Path:
    return common.pdfium_cache_dir() / TAG.replace("/", "-")


def dll_path() -> Path:
    return tag_dir() / "bin" / "pdfium.dll"


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
            wanted = name == "bin/pdfium.dll" or name.upper().startswith("LICENSE") \
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
    """Returns the path of pdfium.dll, downloading it when absent."""
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
            f"{ASSET} holds no bin/pdfium.dll",
            "the bblanchon archive layout with bin/pdfium.dll",
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
